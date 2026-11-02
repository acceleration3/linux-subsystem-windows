#!/usr/bin/env python3
"""The trace store's tools: fetch the pinned capture/replay tools, record a
functionality's trace in the LSW VM, add it to the store, give it reference
pictures.

  traces.py fetch-tools [--from DIR ...]     the pinned tools into the store (builder)
  traces.py list                             what the store holds
  traces.py check [APP[/FUNC] ...]           manifests, files and references
  traces.py push-tools                       the tools and guest scripts into the running VM
  traces.py capture APP/FUNC --tool T ...    trace-capture.ps1 started in the VM's desktop
  traces.py pull APP/FUNC [--title ...]      a finished capture from the VM into the store
  traces.py frames APP/FUNC FRAMES.TXT       a frames.txt the suite listed, into the store
  traces.py mend [APP[/FUNC] ...]            apitrace traces mended where d3dretrace cannot replay them, CLIntercept
                                             kernels made from a device binary given their source (pull does both)
  traces.py replay APP[/FUNC] --out DIR      the traces suite on the running VM, into DIR/traces
  traces.py promote APP/FUNC PASS_DIR        a run's frames become the reference pictures

The VM commands talk to the VM running on this machine - lsw-manager's or
one gpu_matrix.py hold keeps - through LSW's agent and the C$ share, as the
suites do. Nothing downloaded is run on this host: the tools run in the guest.
"""
import argparse
import datetime
import json
import pathlib
import shutil
import sys
import tarfile
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
AV = HERE.parents[1] / "external" / "accel-virt"
sys.path[:0] = [str(HERE), str(AV / "tools"), str(AV / "tools" / "guest"), str(AV / "tools" / "host")]

import cl_sources  # noqa: E402
import trace_mend  # noqa: E402
import trace_store  # noqa: E402
from trace_store import Manifest, Store, StoreError, default_frames, describe_files  # noqa: E402

CAPTURES = "C:\\av\\traces\\capture"
CAPTURE_FILE = {"apitrace": "capture.trace", "clintercept": "Replay", "gfxreconstruct": "capture.gfxr"}
TOOL_DIR_OF = {"apitrace": "apitrace", "clintercept": "clintercept", "gfxr": "gfxreconstruct"}


def say(line):
    print(line, flush=True)


class Vm:
    def __init__(self, args):
        import gpu_matrix
        from app_suites import Desktop
        from lsw_link import GuestShare
        self.work = pathlib.Path(tempfile.mkdtemp(prefix="lsw-traces-"))
        self.guest = gpu_matrix.Guest(self.work, GuestShare(args.user, args.password))
        held = gpu_matrix.bridge_directory("hold") / "agent.sock"
        if held.exists():
            self.guest.agent = gpu_matrix.wait_for_bridge(str(held), time.monotonic() + 30, 600.0)
        else:
            self.guest.connect()
        self.desktop = Desktop(self.guest, self.work / "out", "")
        self.desktop.prepare()

    def close(self):
        self.guest.close()
        shutil.rmtree(self.work, ignore_errors=True)

    def files(self, store):
        import trace_suite
        return trace_suite.GuestFiles(self.desktop, store.root.parent / "trace-staging")

    def push_harness(self):
        import trace_suite
        share = self.guest.share
        share.make_directories(trace_suite.GUEST_HARNESS)
        for name in trace_suite.HARNESS_FILES:
            share.put(trace_suite.ASSETS / name, f"{trace_suite.GUEST_HARNESS}\\{name}")

    def push_tool(self, store, name):
        import trace_suite
        directory, _ = store.tool(name)
        if self.files(store).mirror(directory, f"{trace_suite.GUEST_TOOLS}\\{directory.name}",
                                    trace_store.tree_digest(directory)):
            say(f"sent {directory.name}")
        else:
            say(f"{directory.name} is in the guest already")


def capture_dir(trace_id):
    return f"{CAPTURES}\\{trace_id.replace('/', chr(92))}"


def split_id(trace_id):
    parts = trace_id.split("/")
    if len(parts) != 2 or not all(trace_store.NAME.match(part) for part in parts):
        raise SystemExit(f"{trace_id}: want APP/FUNCTIONALITY, lower-case names")
    return parts


def ps_quote(text):
    text = str(text)
    if '"' in text or text.endswith("\\"):
        raise SystemExit(f"{text!r}: a capture argument cannot hold a double quote or end in a backslash")
    return f'"{text}"'


def capture_api(args):
    api = getattr(args, "api", None) or ("vulkan" if args.tool == "gfxr" else "d3d9")
    wanted = trace_store.GFXR_APIS if args.tool == "gfxr" else trace_store.APITRACE_APIS
    if args.tool in ("apitrace", "gfxr") and api not in wanted:
        raise SystemExit(f"--tool {args.tool} records {', '.join(wanted)}, not {api}")
    return api


def capture_tool(args):
    if args.tool == "gfxr" and capture_api(args) == "d3d12":
        return trace_store.REPLAYER_OF_API["d3d12"]
    return TOOL_DIR_OF[args.tool]


def capture_command(args):
    import trace_suite
    script = f"{trace_suite.GUEST_HARNESS}\\trace-capture.ps1"
    parts = [f"-Tool {args.tool}", f"-Out:{ps_quote(capture_dir(args.trace))}"]
    if args.tool in ("apitrace", "gfxr"):
        parts.append(f"-Api {capture_api(args)}")
    for option, value in (("Exe", args.exe), ("Arguments", args.arguments), ("Package", args.package),
                          ("AppId", args.app_id), ("Process", args.process), ("Mode", args.mode),
                          ("ClKernelName", args.cl_kernel_name), ("GfxrFrames", args.gfxr_frames),
                          ("GfxrTrigger", getattr(args, "gfxr_trigger", "")),
                          ("Environment", ";".join(getattr(args, "capture_env", []) or []))):
        if value:
            parts.append(f"-{option}:{ps_quote(value)}")
    for option, value in (("SteamApp", args.steam_app), ("Seconds", args.seconds), ("ClKernels", args.cl_kernels),
                          ("GfxrTriggerFrames", getattr(args, "gfxr_trigger_frames", 0))):
        if value:
            parts.append(f"-{option} {value}")
    if getattr(args, "gl_trace", ""):
        parts.append(f"-GlOut:{ps_quote(capture_dir(args.gl_trace))}")
    if getattr(args, "clear", None):
        parts.append(f"-ClearFirst:{ps_quote(';'.join(args.clear))}")
    if args.repack:
        parts.append("-Repack")
    return f"powershell -NoProfile -ExecutionPolicy Bypass -File {script} " + " ".join(parts)


def wait_done(share, directory, wait):
    deadline = time.monotonic() + wait
    while True:
        try:
            return share.get_bytes(f"{directory}\\capture.done").decode("utf-8-sig", errors="replace").strip()
        except Exception:  # noqa: BLE001 - LinkError: not written yet
            if time.monotonic() >= deadline:
                return None
            time.sleep(10)


def command_fetch_tools(args):
    store = Store()
    pins = trace_store.load_pins()
    for name, pin in pins.items():
        if args.only and name not in args.only:
            continue
        target, fetched = trace_store.fetch_tool(name, pin, store, sources=args.sources)
        size = sum(path.stat().st_size for path in target.rglob("*") if path.is_file())
        say(f"{name} {pin['version']}: {'unpacked' if fetched else 'up to date'} in {target} ({size / 2**20:.0f} MiB)")
    return 0


def command_list(args):
    store = Store()
    manifests, problems = store.manifests()
    say(f"store: {store.root}")
    for tool in sorted(store.tools_root.glob("*/tool.json")) if store.tools_root.is_dir() else []:
        say(f"  tool {tool.parent.name}")
    for manifest in manifests:
        size = sum(entry.get("size", 0) for entry in manifest.data["files"].values())
        references = manifest.references()
        say(f"  {manifest.id:32} {manifest.api:6} {size / 2**20:9.1f} MiB  frames {manifest.check_frames} "
            f"references {sorted(references)}  {manifest.data.get('title', '')}")
    for problem in problems:
        say(f"  BROKEN {problem}")
    return 0


def command_check(args):
    store = Store()
    manifests, problems = store.select(",".join(args.traces))
    for manifest in manifests:
        missing = manifest.missing_files()
        if missing:
            problems.append(f"{manifest.id}: missing or resized: {', '.join(missing)}")
        if manifest.tool == "apitrace" and manifest.frame_calls() is None:
            problems.append(f"{manifest.id}: no frames.txt (the suite lists frames in the guest every run)")
        unreferenced = [frame for frame in manifest.check_frames if frame not in manifest.references()]
        if unreferenced and manifest.judge["reference"] == "promoted":
            problems.append(f"{manifest.id}: frames {unreferenced} have no reference picture (traces.py promote)")
        try:
            store.tool(manifest.replayer)
        except StoreError as error:
            problems.append(f"{manifest.id}: {error}")
    for problem in problems:
        say(problem)
    say(f"{len(manifests)} trace(s), {len(problems)} problem(s)")
    return 1 if problems else 0


def command_push_tools(args):
    store = Store()
    vm = Vm(args)
    try:
        vm.push_harness()
        for name in args.only or sorted(trace_store.load_pins()):
            vm.push_tool(store, name)
    finally:
        vm.close()
    return 0


def command_capture(args):
    split_id(args.trace)
    if not (args.exe or args.package):
        raise SystemExit("capture needs --exe or --package")
    if args.gl_trace:
        split_id(args.gl_trace)
        if args.tool != "clintercept":
            raise SystemExit("--gl-trace records OpenGL beside a CLIntercept capture (--tool clintercept)")
    command = capture_command(args)
    if args.print:
        say(command)
        return 0
    needs_elevation = args.tool == "clintercept" and (args.mode == "system" or (args.package and args.mode in ("", "auto")))
    if needs_elevation and not args.elevated:
        raise SystemExit("CLIntercept's system mode swaps System32's OpenCL.dll, which needs an elevated shell "
                         "the desktop task does not have: --elevated starts it through desk.py's elevated helper "
                         "(desk.py start), or run what --print prints in an elevated PowerShell in the VM")
    store = Store()
    vm = Vm(args)
    try:
        vm.push_harness()
        vm.push_tool(store, capture_tool(args))
        if args.gl_trace:
            vm.push_tool(store, "apitrace")
        if args.elevated:
            from desk import Desk
            program, _, rest = command.partition(" ")
            say(f"starting elevated through desk.py's helper:\n  {command}")
            quoted = rest.replace("'", "''")
            say(Desk(args.user, args.password).run(
                f"Start-Process -FilePath {program} -WindowStyle Hidden -ArgumentList '{quoted}'; 'started'").strip())
        else:
            say(f"starting in the VM's desktop:\n  {command}")
            vm.desktop.start(command)
        say(f"use the application; the capture ends when it exits"
            + (f" or after {args.seconds} s" if args.seconds else "")
            + f". Then: traces.py pull {args.trace}" + (f" and {args.gl_trace}" if args.gl_trace else ""))
        if args.drive:
            drive(vm, pathlib.Path(args.drive))
        if args.wait:
            status = wait_done(vm.guest.share, capture_dir(args.trace), args.wait)
            say(f"capture: {status or 'still running'}")
    finally:
        vm.close()
    return 0


def drive(vm, script):
    import trace_suite
    library = script.with_name("drive.ps1")
    text = (library.read_text() + "\n" if library.exists() and library != script else "") + script.read_text()
    guest = f"{trace_suite.GUEST_HARNESS}\\drive.ps1"
    vm.guest.share.put_bytes(text.encode("utf-8-sig"), guest)
    say(f"driving the application with {script}")
    code, output = vm.desktop.run(f"powershell -NoProfile -ExecutionPolicy Bypass -File {guest}", 1800)
    for line in output.strip().splitlines():
        say(f"  {line}")
    if code != 0:
        raise SystemExit(f"{script} exited {code} in the VM")


def pull_directory(vm, guest_directory, local):
    code, text = vm.desktop.run(f'dir /s /b /a-d "{guest_directory}"', 300)
    names = [line.strip() for line in text.splitlines() if line.strip().lower().startswith(guest_directory.lower())]
    if code == 0 and 0 < len(names) <= 32:
        for name in names:
            relative = name[len(guest_directory):].lstrip("\\").replace("\\", "/")
            vm.guest.share.get(name, local / relative)
        return
    archive = guest_directory + ".pull.tar"
    vm.desktop.run(f'if exist "{archive}" del "{archive}"', 60)
    code, text = vm.desktop.run(f'C:\\Windows\\System32\\tar.exe -cf "{archive}" -C "{guest_directory}" .', 3600)
    if code != 0:
        raise SystemExit(f"tar in the guest exited {code}: {text.strip()[-300:]}")
    with tempfile.TemporaryDirectory(dir=local.parent) as scratch:
        staged = pathlib.Path(scratch) / "capture.tar"
        vm.guest.share.get(archive, staged)
        with tarfile.open(staged) as tar:
            tar.extractall(local, filter="data")
    vm.desktop.run(f'del "{archive}"', 60)


def command_pull(args):
    app, functionality = split_id(args.trace)
    store = Store()
    target = store.root / app / functionality
    previous = None
    if (target / "manifest.json").exists():
        previous = json.loads((target / "manifest.json").read_text())
        if not args.replace:
            raise SystemExit(f"{target} has a trace already: --replace swaps its capture, keeping its "
                             "title, frames and judge settings")
    vm = Vm(args)
    try:
        directory = capture_dir(args.trace)
        status = wait_done(vm.guest.share, directory, args.wait)
        if status != "ok":
            raise SystemExit(f"the capture in {directory} is {status or 'not finished'}")
        target.parent.mkdir(parents=True, exist_ok=True)
        incoming = target.with_name(target.name + ".incoming")
        shutil.rmtree(incoming, ignore_errors=True)
        incoming.mkdir()
        pull_directory(vm, directory, incoming)
        if not args.keep_guest:
            vm.desktop.run(f'rmdir /s /q "{directory}"', 300)
    finally:
        vm.close()
    captured = json.loads((incoming / "capture.json").read_text(encoding="utf-8-sig"))
    (incoming / "capture.done").unlink(missing_ok=True)
    tool = captured["tool"]
    capture = CAPTURE_FILE[tool]
    if tool == "clintercept" and not (incoming / capture).is_dir():
        capture = "Programs"
    needed = [capture] + (["frames.txt"] if tool == "apitrace" else []) + \
        (["Programs"] if tool == "clintercept" and capture != "Programs" and (incoming / "Programs").is_dir() else [])
    frame_count = captured.get("frames") or 0
    if tool == "gfxreconstruct" and not frame_count and (incoming / "gfxrecon-info.txt").exists():
        frame_count = gfxr_frame_count((incoming / "gfxrecon-info.txt").read_text(errors="replace"))
    mended = trace_mend.mend_file(incoming / capture) if tool == "apitrace" else None
    sourced = cl_sources.source_binaries(incoming / capture, incoming / "Programs") \
        if tool == "clintercept" and capture != "Programs" else ([], [])
    data = previous or {}
    data.update({
        "schema": trace_store.SCHEMA, "app": app, "functionality": functionality,
        "title": args.title or data.get("title") or f"{app} {functionality}",
        "api": captured["api"], "tool": {"name": tool, "version": captured.get("tool_version")},
        "arch": captured.get("arch", "x64"), "capture": capture,
        "files": describe_files(incoming, needed),
    })
    data.setdefault("check", {"frames": first_frames(tool, frame_count)})
    if args.frames is not None:
        data["check"] = {"frames": args.frames}
    data.setdefault("replay", {})
    data.setdefault("judge", {"reference": "warp"} if captured["api"] == "dxgi" else {})
    data["captured"] = dict(captured, how=args.how or (previous or {}).get("captured", {}).get("how", ""),
                            pulled=datetime.datetime.now().isoformat(timespec="seconds"))
    if mended and (mended.mended or mended.dropped):
        data["captured"]["mended"] = mended_note(mended)
        say(f"mended: {data['captured']['mended']}")
    if sourced[0] or sourced[1]:
        data["captured"]["mended"] = cl_sources.note(*sourced)
        say(f"mended: {data['captured']['mended']}")
    (incoming / "manifest.json").write_text(json.dumps(data, indent=1))
    Manifest.load(incoming)
    if target.exists():
        if (target / "reference").is_dir():
            shutil.move(str(target / "reference"), str(incoming / "reference"))
        shutil.rmtree(target)
    incoming.rename(target)
    size = sum(entry["size"] for entry in data["files"].values())
    say(f"{args.trace}: {data['api']} via {tool}, {frame_count} frame(s)/kernel(s), {size / 2**20:.1f} MiB "
        f"in {target}; checking frames {data['check']['frames']}")
    if not data.get("captured", {}).get("how"):
        say("say what was done in the application: traces.py pull ... --replace --how '...' (or edit manifest.json)")
    return 0


def gfxr_frame_count(info):
    import re
    found = re.search(r"Total frames:\s*(\d+)", info)
    return int(found.group(1)) if found else 0


def first_frames(tool, count):
    if tool == "apitrace":
        return default_frames(count)
    if tool == "gfxreconstruct" and count > 1:
        return [frame + 1 for frame in default_frames(count - 1)]
    return []


def mended_note(mender):
    notes = []
    if mender.mended:
        calls = mender.mended
        notes.append(f"pDiscarded widened to NumResources BOOLs in {len(calls)} ReclaimResources call(s) "
                     f"({', '.join(str(number) for number, _, _ in calls[:8])}{', ...' if len(calls) > 8 else ''}): "
                     "apitrace records one, and d3dretrace's room for one overflowed")
    if mender.dropped:
        notes.append(f"the trace ends before {len(mender.dropped)} call(s) the application's exit cut off "
                     f"({', '.join(f'{number} {name}' for number, name in mender.dropped)}), which d3dretrace "
                     "replays anyway")
    return "; ".join(notes)


def command_mend(args):
    manifests, problems = Store().select(",".join(args.traces))
    for manifest in manifests:
        if manifest.tool == "clintercept" and manifest.capture != "Programs":
            sourced, left = cl_sources.source_binaries(manifest.directory / manifest.capture,
                                                       manifest.directory / "Programs")
            if not (sourced or left):
                say(f"{manifest.id}: nothing to mend")
                continue
            manifest.data["files"] = describe_files(manifest.directory, [manifest.capture, "Programs"])
            captured = manifest.data.setdefault("captured", {})
            captured["mended"] = cl_sources.note(sourced, left)
            (manifest.directory / "manifest.json").write_text(json.dumps(manifest.data, indent=1))
            say(f"{manifest.id}: {captured['mended']}")
            continue
        if manifest.tool != "apitrace":
            continue
        mended = trace_mend.mend_file(manifest.directory / manifest.capture)
        if not (mended.mended or mended.dropped):
            say(f"{manifest.id}: nothing to mend")
            continue
        manifest.data["files"].update(describe_files(manifest.directory, [manifest.capture]))
        captured = manifest.data.setdefault("captured", {})
        captured["mended"] = "; ".join(note for note in (captured.get("mended"), mended_note(mended)) if note)
        (manifest.directory / "manifest.json").write_text(json.dumps(manifest.data, indent=1))
        say(f"{manifest.id}: {manifest.data['captured']['mended']}")
    for problem in problems:
        say(problem)
    return 1 if problems else 0


def command_frames(args):
    manifest = Manifest.load(Store().root / args.trace)
    calls = trace_store.parse_frames(pathlib.Path(args.file).read_text())
    if not calls:
        raise SystemExit(f"{args.file} lists no frame")
    (manifest.directory / "frames.txt").write_text("".join(f"{call}\n" for call in calls))
    manifest.data["files"].update(describe_files(manifest.directory, ["frames.txt"]))
    if not manifest.check_frames:
        manifest.data["check"] = {"frames": default_frames(len(calls))}
    (manifest.directory / "manifest.json").write_text(json.dumps(manifest.data, indent=1))
    say(f"{manifest.id}: {len(calls)} frames; checking {manifest.data['check']['frames']}")
    return 0


def command_replay(args):
    import trace_suite
    out = pathlib.Path(args.out)
    vm = Vm(args)
    try:
        env = dict(pair.split("=", 1) for pair in args.env)
        result = trace_suite.run_trace_suite(vm.guest, out / "traces", None, "", ",".join(args.traces), env=env)
    finally:
        vm.close()
    (out / "results.json").write_text(json.dumps({"gpu": {"name": args.gpu_name}, "suites": {"traces": result}},
                                                 indent=1))
    for failure in result["failures"]:
        say(f"FAIL {failure['case']}: {failure['detail']}")
    for note in result["notes"]:
        say(f"note {note['case']}: {note['detail']}")
    for key, value in sorted(result.get("metrics", {}).items()):
        say(f"  {key} = {value}")
    say(f"traces: {result['status']}; results in {out} (traces.py promote APP/FUNC {out} keeps its frames)")
    return 0 if result["status"] == "pass" else 1


def command_promote(args):
    manifest = Manifest.load(Store().root / args.trace)
    pass_dir = pathlib.Path(args.pass_dir)
    source = pass_dir / "traces" / manifest.data["app"] / manifest.data["functionality"]
    if args.warp:
        source = source / "warp"
    pictures = sorted(source.glob("frame-*.png"))
    if not pictures:
        raise SystemExit(f"{source} has no frame-*.png")
    results = pass_dir / "results.json"
    gpu = json.loads(results.read_text())["gpu"]["name"] if results.exists() else "?"
    reference = manifest.directory / "reference"
    shutil.rmtree(reference, ignore_errors=True)
    reference.mkdir()
    for picture in pictures:
        shutil.copy2(picture, reference / picture.name)
    (reference / "reference.json").write_text(json.dumps({
        "source": "WARP, rendered in the guest" if args.warp else f"a replay on {gpu}",
        "pass": str(pass_dir.resolve()), "gpu": gpu,
        "promoted": datetime.datetime.now().isoformat(timespec="seconds")}, indent=1))
    if args.warp:
        manifest.data.setdefault("judge", {})["reference"] = "promoted"
        (manifest.directory / "manifest.json").write_text(json.dumps(manifest.data, indent=1))
    say(f"{manifest.id}: {len(pictures)} reference picture(s) from {source}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    def vm_options(sub):
        sub.add_argument("--user", default="lsw", help="the guest account the share is opened as")
        sub.add_argument("--password", default="lsw")

    fetch = commands.add_parser("fetch-tools", help="the pinned tools into the store")
    fetch.add_argument("--from", dest="sources", action="append", default=[], type=pathlib.Path,
                       help="a directory of archives downloaded already (each verified against its pin)")
    fetch.add_argument("--only", action="append", default=[])
    commands.add_parser("list", help="what the store holds")
    check = commands.add_parser("check", help="manifests, files and references")
    check.add_argument("traces", nargs="*")
    push = commands.add_parser("push-tools", help="the tools into the running VM")
    push.add_argument("--only", action="append", default=[])
    vm_options(push)
    capture = commands.add_parser("capture", help="a capture started in the VM's desktop")
    capture.add_argument("trace", help="APP/FUNCTIONALITY")
    capture.add_argument("--tool", choices=("apitrace", "clintercept", "gfxr"), required=True)
    capture.add_argument("--api", choices=trace_store.APITRACE_APIS + trace_store.GFXR_APIS, default=None,
                         help="apitrace: d3d9 (default), d3d8, dxgi, gl; gfxr: vulkan (default) or d3d12")
    capture.add_argument("--exe", help="the program, as the guest sees it")
    capture.add_argument("--arguments", default="")
    capture.add_argument("--package", help="an MSIX package family name (Canva.Affinity_...)")
    capture.add_argument("--app-id", default="")
    capture.add_argument("--steam-app", type=int, default=0)
    capture.add_argument("--process", default="", help="the process to wait for (default: --exe's name)")
    capture.add_argument("--mode", default="", help="trace-capture.ps1 -Mode (default: auto)")
    capture.add_argument("--seconds", type=int, default=0, help="close the application after this long")
    capture.add_argument("--cl-kernels", type=int, default=0, help="CLIntercept: at most this many enqueues")
    capture.add_argument("--cl-kernel-name", default="")
    capture.add_argument("--gfxr-frames", default="", help="GFXReconstruct: trim to these frames (100-200)")
    capture.add_argument("--gfxr-trigger", default="", metavar="KEY",
                         help="GFXReconstruct: capture nothing until KEY (F12) is pressed - by --drive's script in the scene")
    capture.add_argument("--gfxr-trigger-frames", type=int, default=0, metavar="N",
                         help="GFXReconstruct: with --gfxr-trigger, the frames captured once it is pressed")
    capture.add_argument("--env", dest="capture_env", action="append", default=[], metavar="NAME=VALUE",
                         help="GFXReconstruct: set for the launch, e.g. GFXRECON_MEMORY_TRACKING_MODE=unassisted")
    capture.add_argument("--repack", action="store_true", help="apitrace: recompress the trace with zstd")
    capture.add_argument("--gl-trace", default="", metavar="APP/FUNC",
                         help="CLIntercept: the same run's OpenGL through apitrace's opengl32.dll wrapper, as this trace")
    capture.add_argument("--clear", action="append", default=[], metavar="PATH",
                         help="deleted in the guest before the launch (%%APPDATA%% expands), e.g. a kernel cache")
    capture.add_argument("--elevated", action="store_true",
                         help="start trace-capture.ps1 through desk.py's elevated helper (desk.py start first)")
    capture.add_argument("--print", action="store_true", help="print the guest command instead (for an elevated shell)")
    capture.add_argument("--wait", type=int, default=0, help="seconds to wait for the capture to end")
    capture.add_argument("--drive", default="", help="a PowerShell script that uses the application in the VM's "
                                                     "desktop (scripts/gpu-test/affinity/*.ps1), run once it started")
    vm_options(capture)
    pull = commands.add_parser("pull", help="a finished capture into the store")
    pull.add_argument("trace")
    pull.add_argument("--title", default="")
    pull.add_argument("--how", default="", help="what was done in the application, for whoever records it again")
    pull.add_argument("--frames", type=lambda text: [int(part) for part in text.split(",") if part.strip()],
                      help="frames to judge, comma-separated (default: five spread over the trace)")
    pull.add_argument("--replace", action="store_true")
    pull.add_argument("--keep-guest", action="store_true", help="leave the capture in the guest (default: deleted)")
    pull.add_argument("--wait", type=int, default=0)
    vm_options(pull)
    frames = commands.add_parser("frames", help="a frames.txt into the store")
    frames.add_argument("trace")
    frames.add_argument("file")
    mend = commands.add_parser("mend", help="apitrace traces in the store mended for d3dretrace")
    mend.add_argument("traces", nargs="*", help="apps or APP/FUNC (default: all)")
    replay = commands.add_parser("replay", help="the traces suite on the running VM")
    replay.add_argument("traces", nargs="*", help="apps, APP/FUNC or patterns like resolve/fx-* (default: all)")
    replay.add_argument("--out", required=True)
    replay.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                        help="added to each replay's environment, e.g. AV_D3D11_TRACE=20000")
    replay.add_argument("--gpu-name", default="the running VM's GPU", help="what results.json calls the GPU")
    vm_options(replay)
    promote = commands.add_parser("promote", help="a run's frames become the references")
    promote.add_argument("trace")
    promote.add_argument("pass_dir", help="build/gpu-test/RUN/HOST/GPU-SLUG")
    promote.add_argument("--warp", action="store_true", help="the WARP frames that pass rendered")
    args = parser.parse_args()
    handler = {"fetch-tools": command_fetch_tools, "list": command_list, "check": command_check,
               "push-tools": command_push_tools, "capture": command_capture, "pull": command_pull,
               "frames": command_frames, "mend": command_mend, "replay": command_replay, "promote": command_promote}[args.command]
    try:
        return handler(args)
    except StoreError as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    sys.exit(main())
