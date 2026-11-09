import json
import os
import pathlib
import shutil
import subprocess
import threading
import time
import zipfile

import lsw_link

GUEST_DIR = "C:\\av\\render-run"
QEMU_LOG = "~/.cache/libvirt/qemu/log/lsw.log"
CORES = "~/.local/share/lsw-gpu-test/cores"
RECOVERY_BUDGET = 5


def say(line):
    print(f"[{time.strftime('%H:%M:%S')}] {line}", flush=True)


class Machine:
    def __init__(self, mode, host):
        self.mode = mode
        self.host = host

    def sh(self, script, timeout=300):
        if self.mode == "local":
            command = ["bash", "-s"]
        else:
            control = os.environ.get("LSW_GPU_TEST_CONTROL_PATH", "/tmp/lsw-gpu-test-%C")
            command = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", "-o", f"ControlPath={control}",
                       self.mode.split(":", 1)[1], "bash", "-s"]
        try:
            done = subprocess.run(command, input="export LC_ALL=C\n" + script, capture_output=True, text=True,
                                  timeout=timeout)
        except subprocess.TimeoutExpired:
            return 124, "timed out"
        return done.returncode, done.stdout + done.stderr

    def state(self):
        code, out = self.sh("virsh -c qemu:///session domstate lsw | head -n 1", 60)
        return out.strip() if code == 0 else "unknown"

    def evidence(self, directory, seen):
        directory.mkdir(parents=True, exist_ok=True)
        code, out = self.sh(f"tail -n 120 {QEMU_LOG} 2>&1; echo; virsh -c qemu:///session domstate lsw --reason 2>&1", 60)
        (directory / "qemu-log.txt").write_text(out)
        script = f"""
mkdir -p {CORES}
pid=
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    pid=$(coredumpctl list --no-pager --no-legend --since=-10min qemu-system-x86_64 2>/dev/null | awk '$5 ~ /^[0-9]+$/ {{ p = $5 }} END {{ print p }}')
    case " {' '.join(sorted(seen))} " in *" $pid "*) pid= ;; esac
    [ -n "$pid" ] && break
    sleep 6
done
echo "seen: {' '.join(sorted(seen))}"
[ -n "$pid" ] || {{ echo "no core"; exit 0; }}
case " {' '.join(sorted(seen))} " in *" $pid "*) echo "no new core"; exit 0;; esac
echo "pid $pid"
coredumpctl info "$pid" --no-pager 2>/dev/null | grep -E 'Signal|Timestamp|Command Line|Storage|Message' | head -n 8
file={CORES}/qemu-$pid.core
free=$(df --output=avail -BG ~ | tail -n 1 | tr -dc 0-9)
if [ "${{free:-0}}" -ge 12 ]; then
    nohup coredumpctl dump "$pid" -o "$file" > /dev/null 2>&1 &
    echo "core: $file"
else
    echo "core: kept in the journal only (coredumpctl dump $pid), $free G free"
fi
"""
        code, out = self.sh(script, 240)
        (directory / "coredump.txt").write_text(out)
        info = {"log": str(directory / "qemu-log.txt"), "text": out}
        for line in out.splitlines():
            if line.startswith("pid "):
                info["pid"] = line.split()[1]
            if "Signal:" in line:
                info["signal"] = line.split("Signal:", 1)[1].strip()
            if line.startswith("core:"):
                info["core"] = line[5:].strip()
        return info

    def restart(self):
        return self.sh("virsh -c qemu:///session destroy lsw >/dev/null 2>&1; sleep 3; "
                       "virsh -c qemu:///session start lsw 2>&1", 180)


class Spec:
    def __init__(self, name, adapter, out):
        self.name = name
        self.adapter = adapter
        self.out = pathlib.Path(out)
        self.finished = {}
        self.order = []
        self.retry = []
        self.crashes = []
        self.segments = 0
        self.root = None
        self.outputs = []
        self.aborted = False
        self.stage_exclude = []


class Context:
    def __init__(self, args):
        self.args = args
        self.share = lsw_link.GuestShare(port=args.smb_port)
        self.machine = Machine(args.vm, args.host)
        self.lock = threading.Condition()
        self.generation = 0
        self.recovered = 0
        self.active = 0
        self.recoveries = 0
        self.limit_hit = False
        self.incidents = []
        self.seen_cores = set()
        self.stop = threading.Event()

    def link(self, timeout=60.0):
        return lsw_link.AgentLink(port=self.args.agent_port, timeout=timeout)

    def present(self, name):
        code, out = self.share.smbclient(f'allinfo "{self.share.share_path(GUEST_DIR + chr(92) + name)}"', 60)
        return code == 0 and "attributes:" in out

    def begin(self):
        with self.lock:
            self.active += 1
            return self.generation

    def end(self):
        with self.lock:
            self.active -= 1

    def wait_recovered(self, generation):
        with self.lock:
            while self.recovered <= generation and not self.limit_hit and not self.stop.is_set():
                self.lock.wait(5)
            return not self.limit_hit

    def supervise(self):
        silent = 0
        while not self.stop.is_set():
            time.sleep(10)
            with self.lock:
                if self.active == 0 or self.recovered < self.generation:
                    silent = 0
                    continue
            with self.lock:
                seen = self.generation
            state = self.machine.state()
            reason = None
            if state not in ("running", "unknown"):
                reason = f"the domain is {state}"
            else:
                try:
                    link = self.link(20.0)
                    link.call("system_info", timeout=20)
                    link.close()
                    silent = 0
                except (lsw_link.LinkError, OSError):
                    silent += 10
                    if silent >= self.args.silent_seconds:
                        reason = f"the guest agent has been silent for {silent} s"
            if reason:
                self.recover(reason, seen)
                silent = 0

    def recover(self, reason, seen):
        with self.lock:
            if self.generation != seen:
                return
            self.generation += 1
            self.recoveries += 1
            number = self.recoveries
            over = number > self.args.max_recoveries
        say(f"{self.args.host}: the VM is dead ({reason}); recovery {number} of at most {self.args.max_recoveries}")
        evidence = pathlib.Path(self.args.evidence) / f"{number:03d}"
        info = self.machine.evidence(evidence, self.seen_cores)
        if info.get("pid"):
            self.seen_cores.add(info["pid"])
        record = {"number": number, "reason": reason, "signal": info.get("signal", ""), "core": info.get("core", ""),
                  "pid": info.get("pid", ""), "evidence": self.relative(evidence),
                  "time": time.strftime("%Y-%m-%dT%H:%M:%S")}
        self.incidents.append(record)
        if over:
            say(f"{self.args.host}: the recovery limit of {self.args.max_recoveries} is reached, stopping this run")
            with self.lock:
                self.limit_hit = True
                self.lock.notify_all()
            return
        code, out = self.machine.restart()
        say(f"{self.args.host}: restart: {out.strip()[-120:]}")
        try:
            lsw_link.wait_for_agent(time.monotonic() + 600, port=self.args.agent_port).close()
            self.wait_session()
        except lsw_link.LinkError as error:
            say(f"{self.args.host}: the guest did not come back: {error}")
            with self.lock:
                self.limit_hit = True
                self.lock.notify_all()
            return
        with self.lock:
            self.recovered = self.generation
            self.lock.notify_all()

    def wait_session(self):
        deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            try:
                link = self.link()
                link.call("run_script", {"name": "session probe", "script": "@echo off\r\necho ok\r\n", "session": 1})
                link.close()
                time.sleep(5)
                return
            except (lsw_link.LinkError, OSError):
                time.sleep(10)
        raise lsw_link.LinkError("no console session after the restart")

    def relative(self, path):
        try:
            return str(pathlib.Path(path).relative_to(self.args.root))
        except ValueError:
            return str(path)


def read_text(ctx, name):
    try:
        return ctx.share.get_bytes(f"{GUEST_DIR}\\{name}").decode(errors="replace")
    except lsw_link.LinkError:
        return None


def parse_log(text):
    started, finished = [], {}
    for line in (text or "").splitlines():
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        if entry.get("event") == "start":
            started.append(entry["id"])
        elif "id" in entry and "status" in entry:
            finished[entry["id"]] = entry
    return started, finished


def segment_lines(ctx, spec, number, filter_text, phase, exclude, jobs):
    args = ctx.args
    tag = spec.name
    program = f"lsw-suites{args.bits}.exe"
    flags = [f"--suite {args.suite}", f'--filter "{filter_text}"' if filter_text else "",
             f"--phase {phase}" if phase else "", f"--exclude-file ex-{tag}.txt" if exclude else "",
             f"--jobs {jobs}", "--dump-captures" if args.dump else "",
             f'--adapter "{spec.adapter}"' if spec.adapter else "",
             f"--json r-{tag}-s{number}.json", f"--artifacts art-{tag}", f"--refs {GUEST_DIR}\\render-refs"]
    cleanup = [f"if exist r-{tag}-s{number}.json del r-{tag}-s{number}.json",
               f"if exist r-{tag}-s{number}.jsonl del r-{tag}-s{number}.jsonl",
               f"if exist done-{tag}-s{number}.txt del done-{tag}-s{number}.txt"]
    if number == 1:
        cleanup += [f"if exist art-{tag} rmdir /s /q art-{tag}", f"if exist art-{tag}.zip del art-{tag}.zip"]
    lines = cleanup + [f"{program} {' '.join(f for f in flags if f)} > out-{tag}-s{number}.txt 2>&1",
                       f"echo %ERRORLEVEL% > rc-{tag}-s{number}.txt"]
    return lines, " ".join(f for f in flags if f)


def launch(ctx, name, lines, done):
    script = "@echo off\r\ncd /d " + GUEST_DIR + "\r\n" + "\r\n".join(lines) + f"\r\necho done > {done}\r\n"
    link = ctx.link()
    try:
        link.call("run_script", {"name": f"suite-run {name}", "script": script, "session": 1})
    finally:
        link.close()


def kill(ctx):
    try:
        link = ctx.link()
        link.call("run_script", {"name": "suite-run kill", "script": f"@echo off\r\ntaskkill /f /im lsw-suites{ctx.args.bits}.exe /t\r\n",
                                 "session": 1})
        link.close()
    except (lsw_link.LinkError, OSError):
        pass


def run_segment(ctx, spec, filter_text, phase, exclude, jobs, deadline):
    spec.segments += 1
    number = spec.segments
    tag = spec.name
    if exclude:
        listed = sorted(set(spec.stage_exclude) | set(spec.finished) | set(spec.retry))
        ctx.share.put_bytes(("\r\n".join(listed) + "\r\n").encode(), f"{GUEST_DIR}\\ex-{tag}.txt")
    lines, shown = segment_lines(ctx, spec, number, filter_text, phase, exclude, jobs)
    say(f"{ctx.args.host}: {tag} segment {number}: lsw-suites{ctx.args.bits}.exe {shown}")
    generation = ctx.begin()
    status = "dead"
    try:
        launch(ctx, f"{tag} s{number}", lines, f"done-{tag}-s{number}.txt")
        status = "running"
        while status == "running":
            time.sleep(5)
            if ctx.generation != generation or ctx.limit_hit:
                status = "dead"
            elif time.monotonic() > deadline:
                status = "timeout"
            else:
                try:
                    if ctx.present(f"done-{tag}-s{number}.txt"):
                        status = "done"
                except lsw_link.LinkError:
                    pass
    except (lsw_link.LinkError, OSError):
        status = "dead"
        try:
            probe = ctx.link(20.0)
            probe.call("system_info", timeout=20)
            probe.close()
            status = "error"
        except (lsw_link.LinkError, OSError):
            pass
    finally:
        ctx.end()
    if status == "dead":
        if ctx.generation == generation:
            time.sleep(30)
            ctx.recover("a guest call failed and the guest does not answer", generation)
        if not ctx.wait_recovered(generation):
            spec.aborted = True
    if status == "timeout":
        kill(ctx)
    return number, status, generation


def collect_segment(ctx, spec, number, status):
    tag = spec.name
    text = read_text(ctx, f"r-{tag}-s{number}.jsonl")
    started, finished = parse_log(text)
    result = read_text(ctx, f"r-{tag}-s{number}.json")
    root = None
    if result:
        try:
            root = json.loads(result)
        except ValueError:
            root = None
    if root is not None:
        if spec.root is None:
            spec.root = {key: value for key, value in root.items() if key not in ("checks", "summary")}
        if status == "done":
            for check in root.get("checks", []):
                finished.setdefault(check["id"], check)
    output = read_text(ctx, f"out-{tag}-s{number}.txt") or ""
    spec.outputs.append(f"--- segment {number} ({status}) ---\n{output}")
    for check_id, check in finished.items():
        if check_id not in spec.finished:
            spec.order.append(check_id)
        spec.finished[check_id] = check
    inflight = [i for i in started if i not in finished]
    return list(dict.fromkeys(inflight))


def crash_record(ctx, spec, check_id, incident, confirmed):
    return {"id": check_id, "suite": ctx.args.suite, "tags": [], "status": "crash",
            "reason": (f"host VM died while running (QEMU {incident.get('signal') or 'exit'}), see "
                       f"{incident['evidence']}" + ("; it killed the VM again when run alone" if confirmed else "")),
            "ms": 0, "pid": 0, "started": "", "artifacts": [], "notes": [], "measures": {}, "output": "",
            "host_crash": True, "evidence": incident["evidence"]}


def run_phase(ctx, spec, phase, deadline):
    exclude = bool(spec.stage_exclude)
    while True:
        if spec.aborted or ctx.limit_hit:
            return
        number, status, generation = run_segment(ctx, spec, ctx.args.filter, phase, exclude, ctx.args.jobs, deadline)
        inflight = collect_segment(ctx, spec, number, status)
        exclude = True
        if status != "dead":
            return
        incident = ctx.incidents[-1] if ctx.incidents else {"evidence": "", "signal": ""}
        spec.crashes.append({"segment": number, "incident": incident, "in_flight": inflight})
        for check_id in inflight:
            if check_id not in spec.retry:
                spec.retry.append(check_id)
        say(f"{ctx.args.host}: {spec.name}: the VM died with {len(inflight)} checks in flight; resuming without "
            f"the {len(spec.finished)} finished")


def retry_inflight(ctx, spec, deadline):
    for check_id in list(spec.retry):
        incident = spec.crashes[-1]["incident"] if spec.crashes else {"evidence": "", "signal": ""}
        if spec.aborted or ctx.limit_hit:
            spec.finished[check_id] = crash_record(ctx, spec, check_id, incident, False)
            spec.finished[check_id]["reason"] += "; not retried, the recovery limit was reached"
            spec.order.append(check_id)
            spec.retry.remove(check_id)
            continue
        say(f"{ctx.args.host}: {spec.name}: retrying {check_id} alone")
        spec.retry.remove(check_id)
        number, status, generation = run_segment(ctx, spec, check_id, None, False, 1, deadline)
        collect_segment(ctx, spec, number, status)
        if status == "dead":
            incident = ctx.incidents[-1] if ctx.incidents else incident
            record = crash_record(ctx, spec, check_id, incident, True)
            spec.finished[check_id] = record
            if check_id not in spec.order:
                spec.order.append(check_id)
            spec.crashes.append({"segment": number, "incident": incident, "in_flight": [check_id], "confirmed": check_id})
        elif check_id in spec.finished:
            spec.finished[check_id].setdefault("notes", []).append("in flight when the host VM died; passed the retry alone")


def merged(ctx, spec):
    checks = [spec.finished[i] for i in spec.order if i in spec.finished]
    summary = {"passed": sum(1 for c in checks if c["status"] == "ok"),
               "failed": sum(1 for c in checks if c["status"] not in ("ok", "skip")),
               "skipped": sum(1 for c in checks if c["status"] == "skip")}
    root = dict(spec.root or {"exe": f"lsw-suites{ctx.args.bits}.exe", "adapter": {}})
    root.update({"complete": not spec.aborted, "summary": summary, "segments": spec.segments,
                 "crashes": spec.crashes, "host_crashes": [c["id"] for c in checks if c.get("host_crash")],
                 "incidents": ctx.incidents, "checks": checks})
    return root


def finish(ctx, spec):
    out = spec.out
    out.mkdir(parents=True, exist_ok=True)
    (out / "result.json").write_text(json.dumps(merged(ctx, spec), indent=1))
    (out / "output.txt").write_text("\n".join(spec.outputs))
    tag = spec.name
    last = spec.segments
    try:
        ctx.share.get(f"{GUEST_DIR}\\rc-{tag}-s{last}.txt", out / "rc.txt")
    except lsw_link.LinkError:
        pass
    try:
        ctx.share.remove(f"{GUEST_DIR}\\done-{tag}-zip.txt")
        launch(ctx, f"{tag} zip", [f'powershell -NoProfile -Command "if (Test-Path art-{tag}) {{ Compress-Archive -Path art-{tag} -DestinationPath art-{tag}.zip -Force }}"'], f"done-{tag}-zip.txt")
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline and not ctx.present(f"done-{tag}-zip.txt"):
            time.sleep(5)
        archive = out / "artifacts.zip"
        ctx.share.get(f"{GUEST_DIR}\\art-{tag}.zip", archive)
        with zipfile.ZipFile(archive) as opened:
            for member in opened.infolist():
                target = out / member.filename.replace("\\", "/")
                if member.is_dir() or member.filename.endswith(("/", "\\")):
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(opened.read(member))
        archive.unlink()
        if (out / f"art-{tag}").is_dir():
            shutil.rmtree(out / "artifacts", ignore_errors=True)
            (out / f"art-{tag}").rename(out / "artifacts")
    except (lsw_link.LinkError, OSError, zipfile.BadZipFile) as error:
        say(f"{ctx.args.host}: {tag}: no artifacts archive ({error})")


def discover(ctx):
    program = f"lsw-suites{ctx.args.bits}.exe"
    launch(ctx, "list", [f"{program} --list > list.txt 2>&1"], "done-list.txt")
    deadline = time.monotonic() + 300
    while time.monotonic() < deadline and not ctx.present("done-list.txt"):
        time.sleep(3)
    text = read_text(ctx, "list.txt") or ""
    isolated, others = [], []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 3 or not parts[0].startswith(ctx.args.suite + "."):
            continue
        tags = next((p[5:] for p in parts if p.startswith("tags=")), "").split(",")
        (isolated if parts[-1] == "exclusive" or "pacing" in tags or "exclusive" in tags else others).append(parts[0])
    if not isolated and not others:
        raise lsw_link.LinkError("the check list could not be read from the guest")
    return isolated, others


def execute(args, specs):
    ctx = Context(args)
    deadline = time.monotonic() + args.timeout
    watcher = threading.Thread(target=ctx.supervise, daemon=True)
    watcher.start()
    phases = [p.split(":") + ["parallel"] for p in args.phases.split(",")]
    isolated = others = None
    if any(entry[0] in ("main", "isolated") for entry in phases):
        isolated, others = discover(ctx)
        say(f"{args.host}: {len(others)} checks run on every adapter at once, {len(isolated)} exclusive or pacing checks "
            "run one adapter at a time")
    try:
        for entry in phases:
            phase = None
            for spec in specs:
                spec.stage_exclude = isolated if entry[0] == "main" else others if entry[0] == "isolated" else []
            serial = entry[1] == "serial"
            groups = [[spec] for spec in specs] if serial else [specs]
            for group in groups:
                threads = [threading.Thread(target=run_phase, args=(ctx, spec, phase, deadline)) for spec in group]
                for thread in threads:
                    thread.start()
                for thread in threads:
                    thread.join()
                for spec in group:
                    retry_inflight(ctx, spec, deadline)
    finally:
        ctx.stop.set()
    for spec in specs:
        finish(ctx, spec)
    if ctx.limit_hit:
        say(f"{args.host}: stopped: the recovery limit ({args.max_recoveries}) was reached or the guest did not come back")
        return 1
    return 0
