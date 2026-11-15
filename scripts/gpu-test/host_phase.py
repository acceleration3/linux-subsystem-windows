#!/usr/bin/env python3
"""The host phase of the suites run: sanity.host.* (PLAN.md 4.24).

Around the guest run the matrix snapshots the host stack (the device and executor state dumps, their processes, the
QEMU process, the host GPU memory), marks the avgpu, QEMU and kernel logs, and samples the stall detector. After the
run it waits for the guest to settle and judges each 4.24 row as a contract. Everything that judges takes plain data,
so the tests feed canned dumps.
"""
import csv
import dataclasses
import os
import pathlib
import re
import socket
import subprocess
import threading
import time

ROWS = (
    "sanity.host.alive.stack_stays_up",
    "sanity.host.alive.responsive_after_run",
    "sanity.host.baseline.memory_returns",
    "sanity.host.baseline.objects_return",
    "sanity.host.baseline.threads_fds_return",
    "sanity.host.errors.none_recorded",
    "sanity.host.stalls.none_over_threshold",
    "sanity.host.accounting.bytes_agree",
    "sanity.host.accounting.submissions_agree",
    "sanity.host.accounting.processes_agree",
    "sanity.host.accounting.memory_peak_covers",
    "sanity.host.shutdown.clean_exit",
    "sanity.present.mirror.host_cadence",
)
MIRROR_ROW = ROWS[-1]

STATE_FIELD = re.compile(r"(\w+)=(\S*)")
MODULES = ("device", "executor")

GPU_FAULT = re.compile(
    r"(amdgpu.*(gpu fault|page fault|vm fault|ring \S+ timeout|gpu reset|reset begin|reset succeeded|reset failed)"
    r"|i915.*(gpu hang|gpu reset|\*error\*)"
    r"|\bxe\b.*(gpu reset|reset|fault|timeout|\*error\*)"
    r"|nvrm: xid|nvidia.*(fault|reset|xid))", re.I)
LOG_ERROR = re.compile(r"\b(error|fatal|panic|assert\w*|abort\w*|segfault|device( \d+)? (is )?removed|device lost|stalled)\b", re.I)
QEMU_ERROR = re.compile(r"(\berror\b|failed|assertion|segmentation|aborted|device( \d+)? (is )?removed|device lost|core dumped)", re.I)

HOST_BYTES_FIELDS = ("transferred_bytes", "bulk_bytes")
SUBMISSION_FIELDS = ("submissions", "reports")


@dataclasses.dataclass
class Config:
    settle_seconds: float = 20.0
    respond_seconds: float = 3.0
    rss_tolerance: int = 64 << 20
    gpu_tolerance: int = 256 << 20
    blob_bytes_tolerance: int = 64 << 20
    object_tolerance: int = 4
    thread_tolerance: int = 8
    fd_tolerance: int = 16
    stall_seconds: float = 5.0
    bytes_multiple: float = 8.0
    submission_tolerance: float = 0.5
    benign: tuple = ()
    mirror_event: str = "received"
    mirror_burst_gap_ms: float = 500.0
    mirror_warmup_frames: int = 8
    mirror_p50_tolerance: float = 0.10
    mirror_p99_periods: float = 2.5
    mirror_drop_rate: float = 0.01
    mirror_count_match: float = 0.5


def parse_state(text):
    entries = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        path, _, rest = line.partition(" ")
        segments = path.split("/")
        entries.append({"path": path, "kinds": segments[0::2], "fields": dict(STATE_FIELD.findall(rest))})
    return entries


def number(fields, key, default=0):
    value = fields.get(key, "")
    return int(value) if value.isdigit() else default


def summarize(device_entries, executor_entries):
    device = {"devices": 0, "realized": True, "driver_ok": True, "blobs": 0, "blob_bytes": 0, "contexts": 0,
              "lendings": 0, "lent_pages": 0, "pages_in_use": 0, "peak_pages_in_use": 0, "refusals": 0}
    for entry in device_entries:
        kind, fields = entry["kinds"][-1], entry["fields"]
        if kind == "module":
            device["devices"] = number(fields, "devices")
            device["lendings"] = number(fields, "lendings")
            device["lent_pages"] = number(fields, "lent_pages")
        elif kind == "device":
            device["realized"] = device["realized"] and fields.get("realized") == "true"
            device["driver_ok"] = device["driver_ok"] and fields.get("driver_ok", "true") == "true"
        elif kind == "host_blob":
            device["blobs"] += 1
            device["blob_bytes"] += number(fields, "size")
        elif kind == "context":
            device["contexts"] += 1
        elif kind == "window_stats":
            device["pages_in_use"] += number(fields, "pages_in_use")
            device["peak_pages_in_use"] += number(fields, "peak_pages_in_use")
            device["refusals"] += number(fields, "refusals")
    executor = {"live_sessions": 0, "sessions": 0, "channels": 0, "overflowed": False, "rejected": 0, "refused_other": 0,
                "refused_no_memory": 0, "reports": 0, "counters": {}, "session_pids": set()}
    for entry in executor_entries:
        kind, fields = entry["kinds"][-1], entry["fields"]
        if kind == "executor":
            executor["live_sessions"] = number(fields, "live")
            executor["overflowed"] = fields.get("overflowed") == "true"
        elif kind == "session":
            executor["sessions"] += 1
            if fields.get("pid", "").isdigit():
                executor["session_pids"].add(int(fields["pid"]))
        elif kind == "channel":
            executor["channels"] += 1
            executor["rejected"] += number(fields, "rejected")
            executor["refused_other"] += number(fields, "refused_other")
            executor["refused_no_memory"] += number(fields, "refused_no_memory")
            executor["reports"] += number(fields, "reports")
            for key, value in fields.items():
                if value.isdigit():
                    executor["counters"][key] = executor["counters"].get(key, 0) + int(value)
    return {"device": device, "executor": executor}


class StackProbe:
    def __init__(self, shm="/dev/shm", proc="/proc", timeout=3.0):
        self.shm = pathlib.Path(shm)
        self.proc = pathlib.Path(proc)
        self.timeout = timeout

    def sockets(self):
        found = {}
        for module in MODULES:
            for path in sorted(self.shm.glob(f"av-state-{module}-*.sock")):
                match = re.fullmatch(rf"av-state-{module}-(\d+)\.sock", path.name)
                if match:
                    found.setdefault(module, []).append((int(match.group(1)), path))
        return found

    def request(self, path):
        started = time.monotonic()
        received = bytearray()
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(self.timeout)
            connection.connect(str(path))
            connection.sendall(b"state\n")
            while True:
                data = connection.recv(65536)
                if not data:
                    break
                received += data
        return received.decode(errors="replace"), (time.monotonic() - started) * 1000.0

    def process(self, pid):
        info = {"alive": False}
        try:
            stat = (self.proc / str(pid) / "stat").read_text()
            info["start"] = int(stat.rsplit(")", 1)[1].split()[19])
            status = (self.proc / str(pid) / "status").read_text()
            info["threads"] = int(re.search(r"^Threads:\s+(\d+)", status, re.M).group(1))
            rss = re.search(r"^VmRSS:\s+(\d+) kB", status, re.M)
            info["rss"] = int(rss.group(1)) * 1024 if rss else 0
            info["fds"] = len(list((self.proc / str(pid) / "fd").iterdir()))
            info["alive"] = True
        except (OSError, AttributeError, IndexError, ValueError):
            pass
        return info

    def qemu(self, work):
        marker = str(pathlib.Path(work) / "qmp.sock").encode()
        found = {}
        for entry in self.proc.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                if marker in (entry / "cmdline").read_bytes():
                    found[int(entry.name)] = self.process(int(entry.name))
            except OSError:
                continue
        return found

    def gpu_memory(self):
        total, seen = 0, False
        for path in pathlib.Path("/sys/class/drm").glob("card[0-9]*/device/mem_info_vram_used"):
            try:
                total += int(path.read_text())
                seen = True
            except (OSError, ValueError):
                continue
        return total if seen else None

    def snapshot(self, work=None):
        taken = {"time": time.time(), "modules": {}, "qemu": self.qemu(work) if work else {}, "gpu_memory": self.gpu_memory()}
        texts = {}
        for module, sockets in self.sockets().items():
            for pid, path in sockets:
                entry = {"pid": pid, "process": self.process(pid), "answered": False, "latency_ms": None, "text": ""}
                try:
                    entry["text"], entry["latency_ms"] = self.request(path)
                    entry["answered"] = not entry["text"].startswith("error ")
                except OSError as error:
                    entry["error"] = str(error)
                taken["modules"].setdefault(module, []).append(entry)
                texts.setdefault(module, []).append(entry["text"])
        taken["summary"] = summarize(parse_state("\n".join(texts.get("device", []))),
                                     parse_state("\n".join(texts.get("executor", []))))
        return taken


class LogWindow:
    def __init__(self, path):
        self.path = pathlib.Path(path) if path else None
        self.mark = self.path.stat().st_size if self.path is not None and self.path.exists() else 0

    def delta(self):
        if self.path is None or not self.path.exists():
            return ""
        with open(self.path, "rb") as source:
            size = source.seek(0, os.SEEK_END)
            source.seek(self.mark if size >= self.mark else 0)
            return source.read().decode(errors="replace")


class KernelLog:
    def __init__(self):
        self.since = time.time()

    def delta(self):
        stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(self.since))
        try:
            done = subprocess.run(["journalctl", "-k", "--no-pager", "-o", "short-iso", "--since", stamp],
                                  capture_output=True, text=True, timeout=60)
        except (OSError, subprocess.SubprocessError):
            return None
        return done.stdout if done.returncode == 0 else None


class StallSampler:
    def __init__(self, sample, interval=2.0):
        self.sample = sample
        self.interval = interval
        self.found = []
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)

    def loop(self):
        while not self.stop_event.wait(self.interval):
            try:
                self.found.extend(self.sample())
            except Exception as error:
                self.found.append({"error": str(error)})

    def start(self):
        self.thread.start()

    def finish(self):
        self.stop_event.set()
        self.thread.join(5)
        return self.found


def inspector_stalls(stall_seconds):
    try:
        import av_inspect
    except ImportError:
        return None
    inspector = av_inspect.Inspector()
    probe = StackProbe()

    def sample():
        stalled = []
        for module, sockets in probe.sockets().items():
            for pid, _ in sockets:
                try:
                    described = inspector.host_threads(pid, module, stall_seconds)
                except av_inspect.ToolError:
                    continue
                stalled += [{"module": module, "pid": pid, "thread": t["thread"], "frames": [f["name"] for f in t["frames"]]}
                            for t in described["threads"] if t.get("stalled")]
        return stalled

    return sample


def verdict(row, status, detail=""):
    return {"id": row, "status": status, "detail": detail}


def stack_processes(snapshot):
    return {(module, entry["pid"]): entry["process"].get("start") for module, entries in snapshot["modules"].items()
            for entry in entries}


def judge_alive(before, after):
    row = ROWS[0]
    problems = []
    if not before["modules"]:
        return verdict(row, "fail", "no device or executor state socket existed before the run")
    old, new = stack_processes(before), stack_processes(after)
    for key, start in old.items():
        if key not in new or new[key] is None:
            problems.append(f"{key[0]} process {key[1]} is gone")
        elif new[key] != start:
            problems.append(f"{key[0]} pid {key[1]} was restarted")
    for key in new:
        if key not in old:
            problems.append(f"a new {key[0]} process {key[1]} appeared")
    for pid, process in before["qemu"].items():
        now = after["qemu"].get(pid)
        if now is None or not now.get("alive") or now.get("start") != process.get("start"):
            problems.append(f"QEMU {pid} is gone or restarted")
    return verdict(row, "fail" if problems else "ok", "; ".join(problems))


def judge_responsive(after, config):
    row = ROWS[1]
    problems = []
    if not after["modules"]:
        return verdict(row, "fail", "no state socket answers after the run")
    for module, entries in after["modules"].items():
        for entry in entries:
            if not entry["answered"]:
                problems.append(f"{module} {entry['pid']} did not answer ({entry.get('error', entry['text'][:60])})")
            elif entry["latency_ms"] > config.respond_seconds * 1000:
                problems.append(f"{module} {entry['pid']} answered in {entry['latency_ms']:.0f} ms, over {config.respond_seconds} s")
    if not after["summary"]["device"]["driver_ok"]:
        problems.append("the device reports driver_ok=false")
    return verdict(row, "fail" if problems else "ok", "; ".join(problems))


def judge_memory(before, after, config):
    row = ROWS[2]
    problems = []
    for key, entries in before["modules"].items():
        for entry in entries:
            now = next((e for e in after["modules"].get(key, []) if e["pid"] == entry["pid"]), None)
            if now is None:
                continue
            grown = now["process"].get("rss", 0) - entry["process"].get("rss", 0)
            if grown > config.rss_tolerance:
                problems.append(f"{key} {entry['pid']} resident memory grew by {grown >> 20} MiB")
    grown = after["summary"]["device"]["blob_bytes"] - before["summary"]["device"]["blob_bytes"]
    if grown > config.blob_bytes_tolerance:
        problems.append(f"host blobs hold {grown >> 20} MiB more than before the run")
    pages = after["summary"]["device"]["pages_in_use"] - before["summary"]["device"]["pages_in_use"]
    if pages > config.blob_bytes_tolerance // 4096:
        problems.append(f"{pages} more window pages are in use than before the run")
    if before["gpu_memory"] is not None and after["gpu_memory"] is not None:
        gpu = after["gpu_memory"] - before["gpu_memory"]
        if gpu > config.gpu_tolerance:
            problems.append(f"host GPU memory is {gpu >> 20} MiB above its pre-run value")
    elif not problems:
        return verdict(row, "ok", "host GPU memory is not readable here; process and blob memory returned")
    return verdict(row, "fail" if problems else "ok", "; ".join(problems))


def judge_objects(before, after, config):
    row = ROWS[3]
    problems = []
    for side, keys in (("device", ("blobs", "contexts", "lendings", "lent_pages")),
                       ("executor", ("live_sessions", "sessions", "channels"))):
        for key in keys:
            grown = after["summary"][side][key] - before["summary"][side][key]
            limit = config.object_tolerance * (4096 if key == "lent_pages" else 1)
            if grown > limit:
                problems.append(f"{side} {key} is {after['summary'][side][key]} against {before['summary'][side][key]} before")
    if after["summary"]["executor"]["overflowed"]:
        problems.append("the executor's session table overflowed")
    return verdict(row, "fail" if problems else "ok", "; ".join(problems))


def judge_threads_fds(before, after, config):
    row = ROWS[4]
    problems = []
    for module, entries in before["modules"].items():
        for entry in entries:
            now = next((e for e in after["modules"].get(module, []) if e["pid"] == entry["pid"]), None)
            if now is None:
                continue
            threads = now["process"].get("threads", 0) - entry["process"].get("threads", 0)
            fds = now["process"].get("fds", 0) - entry["process"].get("fds", 0)
            if threads > config.thread_tolerance:
                problems.append(f"{module} {entry['pid']} has {threads} more threads")
            if fds > config.fd_tolerance:
                problems.append(f"{module} {entry['pid']} has {fds} more open files")
    return verdict(row, "fail" if problems else "ok", "; ".join(problems))


def matching(text, pattern, benign):
    return [line.strip() for line in text.splitlines()
            if pattern.search(line) and not any(token in line for token in benign)]


def judge_errors(before, after, logs, config):
    row = ROWS[5]
    problems = []
    kernel = logs.get("kernel")
    if kernel is None:
        problems.append("the host kernel log could not be read (journalctl -k); the GPU fault invariant cannot be judged")
    else:
        faults = matching(kernel, GPU_FAULT, config.benign)
        if faults:
            problems.append(f"{len(faults)} host GPU fault or reset line(s) during the run, first: {faults[0][:200]}")
    for name, pattern in (("avgpu.log", LOG_ERROR), ("qemu log", QEMU_ERROR)):
        text = logs.get("avgpu" if name == "avgpu.log" else "qemu", "")
        found = matching(text, pattern, config.benign)
        if found:
            problems.append(f"{len(found)} error line(s) in the {name}, first: {found[0][:200]}")
    for key in ("rejected", "refused_other"):
        grown = after["summary"]["executor"][key] - before["summary"]["executor"][key]
        if grown > 0:
            problems.append(f"executor counter {key} rose by {grown}")
    if not after["summary"]["device"]["realized"]:
        problems.append("a device is no longer realized")
    return verdict(row, "fail" if problems else "ok", "; ".join(problems))


def judge_stalls(stalls, config):
    row = ROWS[6]
    if stalls is None:
        return verdict(row, "skip", "the stall detector (av_inspect) is not importable on this host")
    real = [s for s in stalls if "error" not in s]
    if real:
        first = real[0]
        return verdict(row, "fail", f"{len(real)} stalled thread sample(s) over {config.stall_seconds} s, first: "
                                    f"{first['module']} {first['pid']} thread {first['thread']} in {' > '.join(first['frames'][-3:])}")
    return verdict(row, "ok")


def counter_total(before, after, names):
    for name in names:
        if name in after["summary"]["executor"]["counters"]:
            return name, after["summary"]["executor"]["counters"][name] - before["summary"]["executor"]["counters"].get(name, 0)
    return None, None


def judge_bytes(before, after, guest, config):
    row = ROWS[7]
    name, host = counter_total(before, after, HOST_BYTES_FIELDS)
    if name is None:
        return verdict(row, "skip", f"the state dump exposes no bytes-transferred counter ({', '.join(HOST_BYTES_FIELDS)})")
    verified = guest.get("bytes_verified")
    if not verified:
        return verdict(row, "skip", "the guest run recorded no bytes_verified measure")
    if host < verified:
        return verdict(row, "fail", f"the host counted {host} bytes ({name}), fewer than the {verified} the guest verified")
    if host > verified * config.bytes_multiple:
        return verdict(row, "fail", f"the host counted {host} bytes, more than {config.bytes_multiple}x the {verified} verified")
    return verdict(row, "ok")


def judge_submissions(before, after, guest, config):
    row = ROWS[8]
    name, host = counter_total(before, after, SUBMISSION_FIELDS)
    made = guest.get("submissions")
    if name is None or not made:
        return verdict(row, "skip", "no host submission counter or no guest submissions measure is available")
    if abs(host - made) > made * config.submission_tolerance:
        return verdict(row, "fail", f"the host counted {host} submissions ({name}) against {made} made by the guest")
    return verdict(row, "ok")


def judge_processes(after, guest):
    row = ROWS[9]
    pids = after["summary"]["executor"]["session_pids"]
    started = set(guest.get("pids", ()))
    if not pids:
        return verdict(row, "skip", "the executor's session entries carry no guest pid")
    if not started:
        return verdict(row, "skip", "the guest run reported no started process ids")
    missing = sorted(started - pids)
    if missing:
        return verdict(row, "fail", f"guest processes {missing[:8]} that used the GPU are not in the host's session list")
    return verdict(row, "ok")


def judge_peak(after, guest):
    row = ROWS[10]
    held = guest.get("held_peak_bytes")
    if not held:
        return verdict(row, "skip", "the guest run recorded no held_peak_bytes measure")
    peak = after["summary"]["device"]["peak_pages_in_use"] * 4096
    if peak < held:
        return verdict(row, "fail", f"the host's peak window use is {peak} bytes, below the {held} the guest held")
    return verdict(row, "ok")


def judge_shutdown(stop):
    row = ROWS[11]
    if stop is None:
        return verdict(row, "skip", "this run did not stop the stack")
    problems = [f"{name} exited with status {status}" for name, status in stop.get("exit_status", {}).items() if status != 0]
    problems += [f"stale file {name}" for name in stop.get("stale", [])]
    return verdict(row, "fail" if problems else "ok", "; ".join(problems))


def feed_events(directory, since=None):
    if not directory:
        return None
    events = []
    for path in sorted(pathlib.Path(directory).glob("feed-*.csv")):
        try:
            if since is not None and path.stat().st_mtime < since:
                continue
            with open(path, newline="") as source:
                for row in csv.DictReader(source):
                    events.append({"event": row["event"], "us": int(row["local_us"]), "frame": int(row["frame"]),
                                   "window": int(row["detail"])})
        except (OSError, KeyError, ValueError):
            continue
    return events


def bursts_of(times_ms, gap_ms):
    bursts, current = [], []
    for when in sorted(times_ms):
        if current and when - current[-1] > gap_ms:
            bursts.append(current)
            current = []
        current.append(when)
    if current:
        bursts.append(current)
    return bursts


def percentile(values, fraction):
    ordered = sorted(values)
    position = fraction * (len(ordered) - 1)
    low = int(position)
    high = min(len(ordered) - 1, low + 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def matching_burst(events, count, config):
    by_window = {}
    for event in events:
        if event["event"] == config.mirror_event:
            by_window.setdefault(event["window"], []).append(event["us"] / 1000.0)
    best = None
    for window, times in by_window.items():
        for burst in bursts_of(times, config.mirror_burst_gap_ms):
            miss = abs(len(burst) - count) / count
            if best is None or miss < best[0]:
                best = (miss, window, burst)
    if best is None or best[0] > config.mirror_count_match:
        return None
    return best[1], best[2]


def judge_mirror_cadence(guest, events, config):
    row = MIRROR_ROW
    frames = (guest or {}).get("frames") or []
    period = (guest or {}).get("period_ms") or 0.0
    if len(frames) <= config.mirror_warmup_frames + 2 or period <= 0:
        return verdict(row, "skip", "the guest run recorded no mirror frame list")
    if events is None:
        return verdict(row, "skip", "no remotewin frame timing directory (RWIN_TIMING_DIR) was set for the host")
    if not [e for e in events if e["event"] == config.mirror_event]:
        return verdict(row, "skip", f"the host wrote no '{config.mirror_event}' events (the mirror process may still be running)")
    presented = len(frames) - config.mirror_warmup_frames
    found = matching_burst(events, len(frames), config)
    if found is None:
        return verdict(row, "fail", f"no host arrival burst resembles the {len(frames)} frames the guest presented")
    window, burst = found
    arrivals = burst[config.mirror_warmup_frames:]
    problems = []
    gaps = [b - a for a, b in zip(arrivals, arrivals[1:])]
    if len(gaps) < 10:
        return verdict(row, "fail", f"only {len(arrivals)} frames arrived in the matching burst of window {window}")
    p50, p99 = percentile(gaps, 0.5), percentile(gaps, 0.99)
    if abs(p50 - period) > period * config.mirror_p50_tolerance:
        problems.append(f"median arrival interval {p50:.2f} ms is not within {config.mirror_p50_tolerance:.0%} of the {period:.2f} ms period")
    if p99 >= period * config.mirror_p99_periods:
        problems.append(f"p99 arrival interval {p99:.2f} ms is not under {config.mirror_p99_periods} periods ({period * config.mirror_p99_periods:.2f} ms)")
    dropped = max(0, presented - len(arrivals))
    if dropped > presented * config.mirror_drop_rate:
        problems.append(f"{dropped} of {presented} presented frames never arrived on the host, above {config.mirror_drop_rate:.0%}")
    detail = "; ".join(problems) if problems else f"window {window}: {len(arrivals)}/{presented} arrived, p50 {p50:.2f} ms, p99 {p99:.2f} ms"
    return verdict(row, "fail" if problems else "ok", detail)


def judge(before, after, run, config=None):
    config = config or Config()
    guest = run.get("guest", {})
    return [judge_alive(before, after), judge_responsive(after, config), judge_memory(before, after, config),
            judge_objects(before, after, config), judge_threads_fds(before, after, config),
            judge_errors(before, after, run.get("logs", {}), config), judge_stalls(run.get("stalls"), config),
            judge_bytes(before, after, guest, config), judge_submissions(before, after, guest, config),
            judge_processes(after, guest), judge_peak(after, guest), judge_shutdown(run.get("stop")),
            judge_mirror_cadence(guest.get("mirror"), run.get("mirror_events"), config)]


def settled(probe, work, config, sleep=time.sleep, clock=time.monotonic):
    deadline = clock() + config.settle_seconds
    last = probe.snapshot(work)
    while clock() < deadline:
        sleep(1.0)
        current = probe.snapshot(work)
        steady = current["summary"] == last["summary"]
        last = current
        if steady and clock() + config.settle_seconds / 2 >= deadline:
            break
    return last


class HostPhase:
    def __init__(self, work, avgpu_log, qemu_log, out, config=None, probe=None, timing_dir=None):
        self.work = work
        self.timing_dir = timing_dir or os.environ.get("RWIN_TIMING_DIR")
        self.started = time.time()
        self.out = pathlib.Path(out)
        self.config = config or Config()
        self.probe = probe or StackProbe()
        self.avgpu = LogWindow(avgpu_log)
        self.qemu = LogWindow(qemu_log)
        self.kernel = KernelLog()
        self.before = self.probe.snapshot(work)
        sample = inspector_stalls(self.config.stall_seconds)
        self.stalls = StallSampler(sample) if sample is not None else None
        if self.stalls is not None:
            self.stalls.start()

    def finish(self, guest=None):
        stalls = self.stalls.finish() if self.stalls is not None else None
        after = settled(self.probe, self.work, self.config)
        logs = {"avgpu": self.avgpu.delta(), "qemu": self.qemu.delta(), "kernel": self.kernel.delta()}
        run = {"logs": logs, "stalls": stalls, "guest": guest or {}, "mirror_events": feed_events(self.timing_dir, self.started)}
        verdicts = judge(self.before, after, run, self.config)
        self.out.mkdir(parents=True, exist_ok=True)
        for name, text in logs.items():
            if text:
                (self.out / f"host-{name}.log").write_text(text)
        return verdicts


def guest_metrics(paths):
    import json
    metrics = {"pids": set(), "bytes_verified": 0, "submissions": 0, "held_peak_bytes": 0, "mirror": None}
    for path in paths:
        try:
            report = json.loads(pathlib.Path(path).read_text())
        except (OSError, ValueError):
            continue
        for check in report.get("checks", []):
            if isinstance(check.get("pid"), int):
                metrics["pids"].add(check["pid"])
            measures = check.get("measures") or {}
            for key in ("bytes_verified", "submissions"):
                if isinstance(measures.get(key), int):
                    metrics[key] += measures[key]
            if check.get("id") == MIRROR_ROW and isinstance(measures.get("frames"), list):
                metrics["mirror"] = {"frames": measures["frames"], "period_ms": measures.get("period_ms", 0.0)}
            if isinstance(measures.get("held_peak_bytes"), int):
                metrics["held_peak_bytes"] = max(metrics["held_peak_bytes"], measures["held_peak_bytes"])
    return metrics


def to_matrix(verdicts):
    failures = [{"case": v["id"], "detail": v["detail"]} for v in verdicts if v["status"] == "fail"]
    notes = [{"case": v["id"], "detail": f"skipped: {v['detail']}"} for v in verdicts if v["status"] == "skip"]
    return failures, notes
