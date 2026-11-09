#!/usr/bin/env python3
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "gpu-test"))
import lsw_link  # noqa: E402

GUEST_DIR = "C:\\av\\render-run"


def present(share, guest_path):
    code, out = share.smbclient(f'allinfo "{share.share_path(guest_path)}"', 60)
    return code == 0 and "attributes:" in out


def make_directory(share, guest_path):
    parts = [p for p in share.share_path(guest_path).split("\\") if p]
    share.smbclient(";".join(f'mkdir "\\{chr(92).join(parts[:i])}"' for i in range(1, len(parts) + 1)), 60)
    if not present(share, guest_path):
        raise lsw_link.LinkError(f"could not make {guest_path}")


def bat(link, share, name, lines, done, deadline):
    share.smbclient(f'del "{share.share_path(GUEST_DIR + chr(92) + done)}"', 30)
    script = "@echo off\r\ncd /d " + GUEST_DIR + "\r\n" + "\r\n".join(lines) + "\r\necho done > " + done + "\r\n"
    link.call("run_script", {"name": name, "script": script, "session": 1})
    while time.monotonic() < deadline:
        time.sleep(5)
        if present(share, GUEST_DIR + "\\" + done):
            return True
    return False


def zip_directory(source, target):
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(pathlib.Path(source).rglob("*")):
            if path.is_file() and ".logs" not in path.parts and path.suffix != ".png":
                archive.write(path, path.relative_to(source).as_posix())


def retry(action, attempts=6, pause=10):
    for attempt in range(attempts):
        try:
            return action()
        except lsw_link.LinkError:
            if attempt == attempts - 1:
                raise
            time.sleep(pause)


def command_push(args):
    share = lsw_link.GuestShare()
    link = lsw_link.AgentLink(timeout=60.0)
    deadline = time.monotonic() + args.timeout
    retry(lambda: make_directory(share, GUEST_DIR))
    for pair in args.exe:
        bits, _, path = pair.partition("=")
        print(f"push lsw-suites{bits}.exe")
        share.put(path, f"{GUEST_DIR}\\lsw-suites{bits}.exe")
    for library in ("dxcompiler.dll", "dxil.dll"):
        source = pathlib.Path(args.dxc) / library
        if source.exists():
            print(f"push {library}")
            share.put(str(source), f"{GUEST_DIR}\\{library}")
        else:
            print(f"warning: {source} is missing, d3d12 rows may fail", file=sys.stderr)
    if args.refs:
        with tempfile.TemporaryDirectory() as scratch:
            archive = os.path.join(scratch, "render-refs.zip")
            zip_directory(args.refs, archive)
            print(f"push render-refs.zip ({os.path.getsize(archive) >> 20} MiB)")
            share.put(archive, f"{GUEST_DIR}\\render-refs.zip")
        finished = bat(link, share, "render-run refs", [
            "if exist render-refs rmdir /s /q render-refs",
            'powershell -NoProfile -Command "Expand-Archive -Force -Path render-refs.zip -DestinationPath render-refs"',
        ], "done-push.txt", deadline)
        if not finished:
            print("the guest did not unpack the references in time", file=sys.stderr)
            return 1
        if not present(share, GUEST_DIR + "\\render-refs"):
            print("the references did not unpack", file=sys.stderr)
            return 1
    return 0


def command_run(args):
    share = lsw_link.GuestShare()
    link = lsw_link.AgentLink(timeout=60.0)
    tag = args.tag
    deadline = time.monotonic() + args.timeout
    program = f"lsw-suites{args.bits}.exe"
    flags = ["--suite render", f'--filter "{args.filter}"' if args.filter else "", f"--jobs {args.jobs}",
             "--dump-captures" if args.dump else "", f'--adapter "{args.adapter}"' if args.adapter else "",
             f"--json r-{tag}.json", f"--artifacts art-{tag}", f"--refs {GUEST_DIR}\\render-refs"]
    lines = [f"if exist art-{tag} rmdir /s /q art-{tag}", f"if exist art-{tag}.zip del art-{tag}.zip",
             f"if exist r-{tag}.json del r-{tag}.json", f"if exist rc-{tag}.txt del rc-{tag}.txt",
             f"{program} {' '.join(f for f in flags if f)} > out-{tag}.txt 2>&1", f"echo %ERRORLEVEL% > rc-{tag}.txt",
             f'powershell -NoProfile -Command "if (Test-Path art-{tag}) {{ Compress-Archive -Path art-{tag} -DestinationPath art-{tag}.zip -Force }}"']
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    print(f"run {program} {tag}: {' '.join(f for f in flags if f)}")
    finished = bat(link, share, f"render-run {tag}", lines, f"done-{tag}.txt", deadline)
    if not finished:
        try:
            link.call("run_script", {"name": "render-run kill", "script": f"@echo off\r\ntaskkill /f /im {program} /t\r\n", "session": 1})
        except lsw_link.LinkError:
            pass
        print(f"the guest run {tag} did not finish within {args.timeout} s; killed {program}", file=sys.stderr)
    status = 0 if finished else 1
    for name, target in ((f"out-{tag}.txt", "output.txt"), (f"r-{tag}.json", "result.json"), (f"rc-{tag}.txt", "rc.txt")):
        try:
            share.get(f"{GUEST_DIR}\\{name}", out / target)
        except lsw_link.LinkError as error:
            print(f"could not fetch {name}: {error}", file=sys.stderr)
            if target == "result.json":
                status = 1
    archive = out / "artifacts.zip"
    try:
        share.get(f"{GUEST_DIR}\\art-{tag}.zip", archive)
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
            if (out / "artifacts").exists():
                import shutil
                shutil.rmtree(out / "artifacts")
            (out / f"art-{tag}").rename(out / "artifacts")
    except lsw_link.LinkError:
        print("no artifacts archive", file=sys.stderr)
    return status


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    push = commands.add_parser("push")
    push.add_argument("--exe", action="append", default=[], metavar="BITS=PATH")
    push.add_argument("--dxc", required=True)
    push.add_argument("--refs")
    push.add_argument("--timeout", type=int, default=900)
    run = commands.add_parser("run")
    run.add_argument("--bits", required=True, choices=("64", "32"))
    run.add_argument("--adapter", default="")
    run.add_argument("--filter", default="")
    run.add_argument("--jobs", type=int, default=2)
    run.add_argument("--dump", action="store_true")
    run.add_argument("--tag", required=True)
    run.add_argument("--out", required=True)
    run.add_argument("--timeout", type=int, default=1400)
    args = parser.parse_args()
    try:
        return command_push(args) if args.command == "push" else command_run(args)
    except (lsw_link.LinkError, subprocess.SubprocessError, OSError) as error:
        print(f"render-run-guest: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
