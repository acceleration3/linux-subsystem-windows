"""The traces suite: API traces recorded from real applications, replayed on
one GPU in the guest gpu_matrix.py booted on it, and judged unattended.

  traces   every functionality in the trace store (trace_store.py), or those
           --traces names: apitrace traces (D3D8/9/10/11, OpenGL) through
           d3dretrace/glretrace, GFXReconstruct captures (Vulkan, D3D12)
           through gfxrecon-replay, CLIntercept kernel captures (OpenCL)
           through their own run.py

Each replay runs in the console session under guest/trace-replay.ps1, which
samples the process's memory; the verdict (trace_judge.py) is the replay's
exit, a lost device or a GPU reset, the frames the manifest names against
its reference pictures, the OpenCL outputs against the application's own,
memory growth and, as a measurement, the replay's frame rate.

The guest keeps what it was sent under C:\\av\\traces, each directory stamped
with what it holds, so a trace crosses the share once per host and VM.
"""
import json
import math
import pathlib
import re
import tarfile
import tempfile
import time

import run_guest_suite as suite
import trace_judge as judge
from app_suites import Desktop, say, verdict
from guest_agent import AgentError
from lsw_link import LinkError
from trace_store import Store, StoreError, resolve_frames, tree_digest

HERE = pathlib.Path(__file__).resolve().parent
ASSETS = HERE / "guest"
TRACE_SUITES = ("traces",)
GUEST_ROOT = "C:\\av\\traces"
GUEST_TOOLS = GUEST_ROOT + "\\tools"
GUEST_STORE = GUEST_ROOT + "\\store"
GUEST_RUNS = GUEST_ROOT + "\\runs"
GUEST_HARNESS = GUEST_ROOT + "\\harness"
HARNESS_FILES = ("trace-replay.ps1", "trace-capture.ps1", "clreplay.py")
TAR = "C:\\Windows\\System32\\tar.exe"
TAR_ABOVE = 16
"""A directory of more files than this crosses the share as one tar."""
NOT_SENT = {"reference", "capture.done"}
"""What stays on the host: reference pictures are judged here."""
RETRACERS = {"d3d8": "d3dretrace.exe", "d3d9": "d3dretrace.exe", "dxgi": "d3dretrace.exe", "gl": "glretrace.exe"}
MODULES = r"^(av\w+|d3d10warp|d3d9|d3d11|d3d12|dxgi|opengl32|vulkan-1|opencl|nvwgf2um\w*|igd\w+|amdxx\w*|atidxx\w*)\.dll$"
"""The DLLs a replay's process is watched for: whose driver it rendered with."""
POWERSHELL = {"x64": "powershell", "x86": "C:\\Windows\\SysWOW64\\WindowsPowerShell\\v1.0\\powershell.exe"}
WARNINGS_NOTED = 5
SHORT_TRACE_FRAMES = 200
KEEP_FREE_GIB = 4.0
"""A trace's copy stays in the guest for the next run only while C: keeps
this much free: VM disks are small, and a trace is sent again when needed."""
KEEP_MAX_MIB = 256
"""A trace larger than this is never kept in the guest: the VM's disk grows on
the host's own (the accel host has ~15 GB to spare, and Resolve's OpenCL
traces are 300-800 MiB each, 15 GB together), while sending one again is a
local copy of seconds."""


def guest_path(*parts):
    return "\\".join(part.replace("/", "\\").strip("\\") for part in parts)


class GuestFiles:
    """Directories of the host mirrored into the guest over the share, each
    stamped with a digest of what it holds."""

    def __init__(self, desktop, staging):
        self.desktop = desktop
        self.share = desktop.guest.share
        self.staging = pathlib.Path(staging)

    def stamp(self, directory):
        try:
            return self.share.get_bytes(directory + "\\.stamp").decode(errors="replace").strip()
        except LinkError:
            return None

    def console(self, command, timeout=600):
        code, text = self.desktop.run(command, timeout)
        if code != 0:
            raise LinkError(f"{command}: exit {code}: {text.strip()[-300:]}")
        return text

    def mirror(self, local, directory, digest, skip=NOT_SENT):
        """`local`'s files in guest `directory`, unless it holds `digest` already."""
        if self.stamp(directory) == digest:
            return False
        files = [path for path in sorted(pathlib.Path(local).rglob("*"))
                 if path.is_file() and not (set(path.relative_to(local).parts) & skip)]
        self.console(f'if exist "{directory}" rmdir /s /q "{directory}"')
        self.share.make_directories(directory)
        if len(files) > TAR_ABOVE:
            self.staging.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=self.staging, suffix=".tar", delete=False) as staged:
                bundle = pathlib.Path(staged.name)
            try:
                with tarfile.open(bundle, "w", format=tarfile.PAX_FORMAT) as tar:
                    for path in files:
                        tar.add(path, arcname=path.relative_to(local).as_posix(), recursive=False)
                upload = directory + "\\.upload.tar"
                self.share.put(bundle, upload)
                self.console(f'{TAR} -xf "{upload}" -C "{directory}" && del "{upload}"', 3600)
            finally:
                bundle.unlink(missing_ok=True)
        else:
            made = {directory}
            for path in files:
                target = guest_path(directory, path.relative_to(local).as_posix())
                parent = target.rsplit("\\", 1)[0]
                if parent not in made:
                    self.share.make_directories(parent)
                    made.add(parent)
                self.share.put(path, target)
        self.share.put_bytes(digest.encode(), directory + "\\.stamp")
        return True

    def listing(self, directory):
        """The file names in a guest directory."""
        code, text = self.desktop.run(f'dir /b /a-d "{directory}"', 120)
        return [line.strip() for line in text.splitlines() if line.strip()] if code == 0 else []


class Replayer:
    """One trace's replay passes in the guest, their results pulled to `out`."""

    def __init__(self, files, manifest, out, tools, env=None):
        self.env = dict(env or {})
        self.files = files
        self.desktop = files.desktop
        self.manifest = manifest
        self.out = out
        self.tools = tools
        self.store_dir = guest_path(GUEST_STORE, manifest.id)
        self.run_dir = guest_path(GUEST_RUNS, manifest.id)
        out.mkdir(parents=True, exist_ok=True)

    def tool_dir(self, name):
        directory, pin = self.tools[name]
        return guest_path(GUEST_TOOLS, directory.name)

    def prepare(self):
        self.files.console(f'if exist "{self.run_dir}" rmdir /s /q "{self.run_dir}"')
        self.files.share.make_directories(self.run_dir)

    def run(self, name, exe, arguments, env=None, timeout=None, binary_stdout=False, arch="x64", linger=None):
        """One pass: trace-replay.ps1's result, with its stderr (and its stdout,
        unless that is a binary stream, which is left in out/NAME.out)."""
        timeout = timeout or self.manifest.replay["timeout_s"]
        config = {"exe": exe, "args": suite_command_line(arguments), "cwd": self.store_dir,
                  "env": {**self.manifest.replay["env"], **self.env, **(env or {})},
                  "stdout": f"{self.run_dir}\\{name}.out", "stderr": f"{self.run_dir}\\{name}.err",
                  "result": f"{self.run_dir}\\{name}.json", "timeout_s": timeout, "sample_ms": 500,
                  "modules": MODULES, "linger_s": linger}
        self.files.share.put_bytes(json.dumps(config).encode(), f"{self.run_dir}\\{name}.config.json")
        say(f"  {name}: {pathlib.PureWindowsPath(exe).name} {config['args'][:160]}")
        self.desktop.run(f'{POWERSHELL[arch]} -NoProfile -ExecutionPolicy Bypass -File {GUEST_HARNESS}\\trace-replay.ps1 '
                         f'-Config "{self.run_dir}\\{name}.config.json"', timeout + 600)
        try:
            result = json.loads(self.files.share.get_bytes(config["result"]).decode("utf-8-sig"))
        except (LinkError, ValueError):
            return None
        output = ""
        for stream in ("out", "err"):
            local = self.out / f"{name}.{stream}"
            try:
                self.files.share.get(f"{self.run_dir}\\{name}.{stream}", local)
            except LinkError:
                continue
            if stream == "err" or not binary_stdout:
                output += local.read_bytes().decode(errors="replace")
        result["output"] = output
        (self.out / f"{name}.result.json").write_text(json.dumps({key: value for key, value in result.items()
                                                                  if key != "output"}, indent=1))
        return result


def suite_command_line(arguments):
    """Arguments as the Microsoft C runtime splits them back."""
    import subprocess
    return subprocess.list2cmdline([str(argument) for argument in arguments])


# ------------------------------------------------------------------ replaying

def frames_of_stream(path, call_to_frame):
    """{frame: image} from an apitrace PNM stream whose comments are call numbers."""
    try:
        data = path.read_bytes()
    except OSError:
        return {}
    pictures = {}
    for comment, picture in judge.parse_pnm_stream(data):
        if comment is not None and comment.isdigit() and int(comment) in call_to_frame:
            pictures[call_to_frame[int(comment)]] = picture
    return pictures


def replay_apitrace(replayer):
    """apitrace: a benchmark pass for the frame rate and memory, a pass that
    reads back the frames the manifest names (and prints every failed call),
    and, for a dxgi trace judged against WARP with no promoted reference yet,
    the same frames rendered by WARP."""
    manifest, out = replayer.manifest, replayer.out
    bin_dir = guest_path(replayer.tool_dir("apitrace"), manifest.arch, "bin")
    retracer = guest_path(bin_dir, RETRACERS[manifest.api])
    trace = guest_path(replayer.store_dir, manifest.capture)
    extra = list(manifest.replay["extra_args"])
    findings = {"passes": {}, "notes": [], "frames": {}, "references": {}}
    calls = manifest.frame_calls()
    if calls is None:
        listed = replayer.run("frames", guest_path(bin_dir, "apitrace.exe"),
                              ["dump", "--calls=frame", "--arg-names=no", "--call-nos=yes", trace])
        calls = [int(found) for found in re.findall(r"^\s*(\d+)\s", (listed or {}).get("output", ""), re.M)]
        (out / "frames.txt").write_text("".join(f"{call}\n" for call in calls))
        findings["notes"].append({"case": "frames.txt", "detail": "the store has none; listed in the guest "
                                                                  "(traces.py frames adds it)"})
    findings["frame_count"] = len(calls)
    wanted, outside = resolve_frames(manifest.check_frames, len(calls))
    if outside:
        findings["notes"].append({"case": "frames", "detail": f"{outside} are outside the trace's {len(calls)} frames"})
    call_to_frame = {calls[index]: index for index in wanted}
    callset = ",".join(str(calls[index]) for index in wanted)
    if manifest.replay["benchmark"]:
        findings["passes"]["benchmark"] = replayer.run("benchmark", retracer, ["-b", *extra, trace], arch=manifest.arch)
    if 0 < len(calls) < SHORT_TRACE_FRAMES:
        # A replay of a few frames is over before its DLLs can be looked at:
        # one more pass that keeps its window (-w) until it is closed, which
        # is also too short to be timed.
        findings["passes"]["driver"] = replayer.run("driver", retracer, ["-w", *extra, trace], arch=manifest.arch,
                                                    linger=4)
        findings["frame_count"] = None
    if wanted:
        arguments = ["-s", "-", "--snapshot-format=PNM", "-S", callset, *extra, trace]
        findings["passes"]["check"] = replayer.run("check", retracer, arguments, binary_stdout=True, arch=manifest.arch)
        pictures = frames_of_stream(out / "check.out", call_to_frame)
        findings["frames"] = {index: pictures.get(index) for index in wanted}
        (out / "check.out").unlink(missing_ok=True)
    else:
        findings["passes"]["check"] = replayer.run("check", retracer, [*extra, trace], arch=manifest.arch)
    references = {frame: path for frame, path in manifest.references().items() if frame in wanted}
    if not references and wanted and manifest.judge["reference"] == "warp":
        findings["passes"]["warp"] = replayer.run("warp", retracer, ["--driver=sw", "-s", "-", "--snapshot-format=PNM", "-S", callset, *extra,
                                        trace], binary_stdout=True, arch=manifest.arch)
        warp = frames_of_stream(out / "warp.out", call_to_frame)
        (out / "warp.out").unlink(missing_ok=True)
        (out / "warp").mkdir(exist_ok=True)
        for frame, picture in warp.items():
            judge.write_png(out / "warp" / f"frame-{frame}.png", picture)
        findings["references"] = warp
        findings["notes"].append({"case": "reference", "detail": f"rendered by WARP in this run ({len(warp)} frames): "
                                                                 "traces.py promote --warp keeps them"})
    else:
        findings["references"] = {frame: judge.read_png(path) for frame, path in references.items()}
    return findings


def replay_gfxreconstruct(replayer):
    """gfxrecon-replay: one pass, the frames the manifest names as BMP
    screenshots (GFXReconstruct numbers frames from 1), its own FPS line."""
    manifest, out = replayer.manifest, replayer.out
    replay = guest_path(replayer.tool_dir(manifest.replayer), "x64", "gfxrecon-replay.exe")
    capture = guest_path(replayer.store_dir, manifest.capture)
    wanted = [frame for frame in manifest.check_frames if frame > 0]
    findings = {"passes": {}, "notes": [], "frames": {}, "references": {}}
    if len(wanted) != len(manifest.check_frames):
        findings["notes"].append({"case": "frames", "detail": "GFXReconstruct frames count from 1; others ignored"})
    shots = guest_path(replayer.run_dir, "shots")
    arguments = list(manifest.replay["extra_args"])
    if wanted:
        replayer.files.share.make_directories(shots)
        arguments += ["--screenshots", ",".join(str(frame) for frame in wanted), "--screenshot-format", "bmp",
                      "--screenshot-dir", shots, "--screenshot-prefix", "shot"]
    findings["passes"]["check"] = replayer.run("check", replay, [*arguments, capture])
    for name in replayer.files.listing(shots) if wanted else []:
        found = re.match(r"shot_frame_(\d+).*\.bmp$", name)
        if not found or int(found.group(1)) in findings["frames"]:
            continue
        local = out / name
        try:
            replayer.files.share.get(f"{shots}\\{name}", local)
            findings["frames"][int(found.group(1))] = judge.read_bmp(local)
        except (LinkError, OSError, ValueError) as error:
            findings["notes"].append({"case": name, "detail": str(error)})
        local.unlink(missing_ok=True)
    for frame in wanted:
        findings["frames"].setdefault(frame, None)
    findings["references"] = {frame: judge.read_png(path) for frame, path in manifest.references().items()
                              if frame in wanted}
    return findings


def replay_opencl(replayer):
    """CLIntercept: guest/clreplay.py runs every captured kernel's run.py and
    compares what it wrote with the application's own outputs."""
    manifest = replayer.manifest
    python = guest_path(replayer.tool_dir("python"), "x64", "python.exe")
    limits = manifest.judge
    result_path = f"{replayer.run_dir}\\cl-result.json"
    arguments = [f"{GUEST_HARNESS}\\clreplay.py", "--capture", guest_path(replayer.store_dir, manifest.capture),
                 "--programs", guest_path(replayer.store_dir, "Programs"), "--out", result_path, "--platform", manifest.replay["cl_platform"],
                 "--rtol", limits["rtol"], "--atol", limits["atol"], "--itol", limits["itol"]]
    findings = {"passes": {}, "notes": [], "frames": {}, "references": {}}
    findings["passes"]["opencl"] = replayer.run("opencl", python, arguments)
    try:
        findings["opencl"] = json.loads(replayer.files.share.get_bytes(result_path).decode("utf-8-sig"))
    except (LinkError, ValueError) as error:
        findings["opencl"] = {"enqueues": [], "error": f"clreplay.py left no result: {error}"}
    (replayer.out / "cl-result.json").write_text(json.dumps(findings["opencl"], indent=1))
    return findings


REPLAYS = {"apitrace": replay_apitrace, "gfxreconstruct": replay_gfxreconstruct, "clintercept": replay_opencl}


# -------------------------------------------------------------------- judging

def judge_findings(manifest, findings, resets, out):
    """failures, notes and metrics of one trace's replay."""
    limits = manifest.judge
    failures, notes, metrics = [], list(findings["notes"]), {}
    passes = findings["passes"]
    measured = next((name for name in ("benchmark", "check", "opencl") if passes.get(name)), None)
    for name, result in passes.items():
        # Memory is judged once, on the pass that ran the trace as it is;
        # clreplay.py's own process is not what an OpenCL leak would grow.
        judged = name == measured and manifest.tool != "clintercept"
        pass_limits = dict(limits) if judged else dict(limits, memory_growth_mib_max=None)
        if name == "driver" and result and result.get("closed"):
            result = dict(result, exit_code=0)  # closed by trace-replay.ps1 once seen, as asked
        pass_failures, pass_metrics = judge.judge_process(name, result, pass_limits)
        failures += pass_failures
        if name == measured:
            metrics.update(pass_metrics)
    fps = None
    for result in passes.values():
        rate = judge.retrace_fps((result or {}).get("output", ""))
        if rate:
            fps = rate[2]
        gfxr = re.search(r"Measured FPS: ([\d.]+) fps", (result or {}).get("output", ""))
        if gfxr:
            fps = float(gfxr.group(1))
    check = passes.get("check")
    if fps is None and check and check.get("exit_code") == 0 and check.get("seconds") and findings.get("frame_count"):
        fps = findings["frame_count"] / check["seconds"]
    if fps is not None:
        metrics["fps"] = round(fps, 1)
    failures += judge.judge_fps(fps, limits)
    if manifest.tool != "clintercept":
        # Which driver rendered: every pass but the trace listing and WARP's,
        # together (a short pass may end before its DLLs could be seen).
        loaded = sorted({name for pass_name, result in passes.items() if pass_name not in ("frames", "warp")
                         for name in (result or {}).get("modules") or []})
        driver_failures, driver_notes, seen = judge.judge_driver("replay", {"modules": loaded}, manifest.api)
        failures += driver_failures
        notes += driver_notes
        if seen:
            metrics["driver"] = ", ".join(seen)
        if passes.get("warp"):
            failures += judge.judge_driver("warp", passes["warp"], manifest.api, warp=True)[0]
    warnings = judge.retrace_warnings((passes.get("check") or {}).get("output", ""))
    if warnings:
        metrics["retrace_warnings"] = len(warnings)
        notes += [{"case": "retrace", "detail": line} for line in warnings[:WARNINGS_NOTED]]
    for reset in resets:
        failures.append({"case": "gpu reset", "detail": reset})
    frame_failures, frame_notes, frame_metrics = judge.judge_frames(findings["frames"], findings["references"],
                                                                    limits)
    failures += frame_failures
    notes += frame_notes
    metrics.update(frame_metrics)
    summary = {"frames": {}, "hashes": {}, "judge": limits}
    for frame, picture in findings["frames"].items():
        if picture is None:
            continue
        judge.write_png(out / f"frame-{frame}.png", picture)
        summary["frames"][str(frame)] = f"frame-{frame}.png"
        reference = findings["references"].get(frame)
        if reference is not None and reference[:2] == picture[:2] and reference[2] != picture[2]:
            judge.write_png(out / f"diff-{frame}.png", judge.difference_image(reference, picture))
    if "opencl" in findings:
        cl_failures, cl_notes, cl_metrics = judge.judge_opencl(findings["opencl"], limits,
                                                               manifest.replay["cl_platform"])
        failures += cl_failures
        notes += cl_notes
        metrics.update(cl_metrics)
        summary["hashes"] = judge.output_hashes(findings["opencl"])
    (out / "trace.json").write_text(json.dumps(summary, indent=1))
    return failures, notes, metrics


# ---------------------------------------------------------------------- suite

def run_trace_suite(guest, out, gpu, guest_name, selection="", store=None, env=None):
    """Every selected trace replayed on `gpu`: the suite's verdict. `env` is
    added to every replay's environment (AV_D3D11_TRACE, say)."""
    say("suite traces")
    store = store or Store()
    desktop = Desktop(guest, out, guest_name)
    failures, notes, metrics = [], [], {}
    try:
        manifests, problems = store.select(selection)
        failures += [{"case": "store", "detail": problem} for problem in problems]
        if not manifests:
            failures.append({"case": "store", "detail": f"no trace to replay in {store.root}"})
            return finish(out, verdict(failures, notes, metrics))
        desktop.prepare()
        files = GuestFiles(desktop, store.root.parent / "trace-staging")
        files.share.make_directories(GUEST_HARNESS)
        for name in HARNESS_FILES:
            files.share.put(ASSETS / name, f"{GUEST_HARNESS}\\{name}")
        tools = {}
        for manifest in manifests:
            name = manifest.replayer
            if name in tools:
                continue
            try:
                directory, pin = store.tool(name)
            except StoreError as error:
                failures.append({"case": manifest.id, "detail": str(error)})
                continue
            if files.mirror(directory, guest_path(GUEST_TOOLS, directory.name), tree_digest(directory)):
                say(f"sent {directory.name} to the guest")
            tools[name] = (directory, pin)
        for manifest in manifests:
            missing = manifest.missing_files()
            if missing:
                failures.append({"case": manifest.id, "detail": f"the store lacks {', '.join(missing[:5])}"})
                continue
            if manifest.replayer not in tools:
                continue
            say(f"trace {manifest.id} ({manifest.api})")
            trace_out = out / manifest.data["app"] / manifest.data["functionality"]
            started = time.monotonic()
            try:
                if files.mirror(manifest.directory, guest_path(GUEST_STORE, manifest.id), manifest.digest()):
                    say(f"  sent {manifest.id} to the guest")
                replayer = Replayer(files, manifest, trace_out, tools, env)
                replayer.prepare()
                since = desktop.clock()
                findings = REPLAYS[manifest.tool](replayer)
                resets = desktop.resets_since(since)
                trace_failures, trace_notes, trace_metrics = judge_findings(manifest, findings, resets, trace_out)
            except (AgentError, LinkError, suite.StepFailed, OSError, ValueError, StoreError) as error:
                trace_failures = [{"case": "replay", "detail": f"{type(error).__name__}: {error}"}]
                trace_notes, trace_metrics = [], {}
            try:
                size_mib = sum(entry.get("size", 0) for entry in manifest.data["files"].values()) / 2**20
                free = guest_free_gib(desktop) if size_mib <= KEEP_MAX_MIB else None
                if size_mib > KEEP_MAX_MIB or (free is not None and free < KEEP_FREE_GIB):
                    files.console(f'rmdir /s /q "{guest_path(GUEST_STORE, manifest.id)}"')
                    say(f"  removed the guest's copy of {manifest.id}: "
                        + (f"{size_mib:.0f} MiB" if size_mib > KEEP_MAX_MIB else f"C: had {free:.1f} GiB free"))
            except (AgentError, LinkError, suite.StepFailed, OSError) as error:
                say(f"  could not tidy the guest: {error}")
            failures += [dict(entry, case=f"{manifest.id}: {entry['case']}") for entry in trace_failures]
            notes += [dict(entry, case=f"{manifest.id}: {entry['case']}") for entry in trace_notes]
            metrics.update({f"{manifest.id} {key}": value for key, value in trace_metrics.items()})
            say(f"  {manifest.id}: {len(trace_failures)} failure(s) in {time.monotonic() - started:.0f} s")
    except (AgentError, LinkError, suite.StepFailed, OSError) as error:
        failures.append({"case": "traces", "detail": f"{type(error).__name__}: {error}"})
        result = verdict(failures, notes, metrics)
        result["status"] = "error"
        return finish(out, result)
    return finish(out, verdict(failures, notes, metrics))


def guest_free_gib(desktop):
    _, text = desktop.powershell("[math]::Round((Get-PSDrive C).Free / 1GB, 2)", 60)
    lines = text.strip().splitlines()
    try:
        return float(lines[-1].replace(",", ".")) if lines else None
    except ValueError:
        return None


def finish(out, result):
    out.mkdir(parents=True, exist_ok=True)
    for key, value in list(result.get("metrics", {}).items()):
        if isinstance(value, float) and math.isinf(value):
            result["metrics"][key] = "inf"
    (out / "verdict.json").write_text(json.dumps(result, indent=1))
    return result
