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



def guest_dir(run_id):
    return lsw_link.RUN_BASE + "\\" + run_id


def present(share, guest_path):
    code, out = share.smbclient(f'allinfo "{share.share_path(guest_path)}"', 60)
    return code == 0 and "attributes:" in out


def make_directory(share, guest_path):
    parts = [p for p in share.share_path(guest_path).split("\\") if p]
    share.smbclient(";".join(f'mkdir "\\{chr(92).join(parts[:i])}"' for i in range(1, len(parts) + 1)), 60)
    if not present(share, guest_path):
        raise lsw_link.LinkError(f"could not make {guest_path}")


def bat(link, share, directory, name, lines, done, deadline):
    share.smbclient(f'del "{share.share_path(directory + chr(92) + done)}"', 30)
    script = "@echo off\r\ncd /d " + directory + "\r\n" + "\r\n".join(lines) + "\r\necho done > " + done + "\r\n"
    link.call("run_script", {"name": name, "script": script, "session": 1})
    while time.monotonic() < deadline:
        time.sleep(5)
        if present(share, directory + "\\" + done):
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
    share = lsw_link.GuestShare(port=args.smb_port)
    link = lsw_link.AgentLink(port=args.agent_port, timeout=60.0)
    deadline = time.monotonic() + args.timeout
    directory = guest_dir(args.run_id)
    retry(lambda: make_directory(share, directory))
    for pair in args.exe:
        bits, _, path = pair.partition("=")
        print(f"push lsw-suites{bits}.exe")
        share.put(path, f"{directory}\\lsw-suites{bits}.exe")
    for library in ("dxcompiler.dll", "dxil.dll"):
        source = pathlib.Path(args.dxc) / library
        if source.exists():
            print(f"push {library}")
            share.put(str(source), f"{directory}\\{library}")
        else:
            print(f"warning: {source} is missing, d3d12 rows may fail", file=sys.stderr)
    if args.refs:
        with tempfile.TemporaryDirectory() as scratch:
            archive = os.path.join(scratch, "render-refs.zip")
            zip_directory(args.refs, archive)
            print(f"push render-refs.zip ({os.path.getsize(archive) >> 20} MiB)")
            share.put(archive, f"{directory}\\render-refs.zip")
        finished = bat(link, share, directory, "render-run refs", [
            "if exist render-refs rmdir /s /q render-refs",
            'powershell -NoProfile -Command "Expand-Archive -Force -Path render-refs.zip -DestinationPath render-refs"',
        ], "done-push.txt", deadline)
        if not finished:
            print("the guest did not unpack the references in time", file=sys.stderr)
            return 1
        if not present(share, directory + "\\render-refs"):
            print("the references did not unpack", file=sys.stderr)
            return 1
    return 0


def command_run(args):
    import suite_run
    if args.command == "run":
        runs = [(args.tag, args.adapter, args.out)]
    else:
        runs = [tuple(entry.split("|", 2)) for entry in args.run]
    specs = [suite_run.Spec(name, adapter, out) for name, adapter, out in runs]
    return suite_run.execute(args, specs)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    push = commands.add_parser("push")
    push.add_argument("--run-id", required=True)
    push.add_argument("--exe", action="append", default=[], metavar="BITS=PATH")
    push.add_argument("--dxc", required=True)
    push.add_argument("--refs")
    push.add_argument("--timeout", type=int, default=900)
    push.add_argument("--agent-port", type=int, default=lsw_link.AGENT_PORT)
    push.add_argument("--smb-port", type=int, default=lsw_link.SMB_PORT)
    for name in ("run", "run-set"):
        run = commands.add_parser(name)
        run.add_argument("--run-id", required=True)
        run.add_argument("--bits", required=True, choices=("64", "32"))
        run.add_argument("--suite", default="render")
        run.add_argument("--filter", default="")
        run.add_argument("--jobs", type=int, default=2)
        run.add_argument("--dump", action="store_true")
        run.add_argument("--timeout", type=int, default=3600)
        run.add_argument("--agent-port", type=int, default=lsw_link.AGENT_PORT)
        run.add_argument("--smb-port", type=int, default=lsw_link.SMB_PORT)
        run.add_argument("--vm", default="local", help="local, or ssh:USER@HOST for a VM on another machine")
        run.add_argument("--host", default="local")
        run.add_argument("--root", default=".", help="the report tree; evidence paths are relative to it")
        run.add_argument("--evidence", default="evidence")
        run.add_argument("--max-recoveries", type=int, default=5)
        run.add_argument("--silent-seconds", type=int, default=240)
        run.add_argument("--phases", default="all", help="all, or e.g. main:parallel,pacing:serial")
        if name == "run":
            run.add_argument("--adapter", default="")
            run.add_argument("--tag", required=True)
            run.add_argument("--out", required=True)
        else:
            run.add_argument("--run", action="append", required=True, metavar="TAG|ADAPTER|OUTDIR")
    args = parser.parse_args()
    try:
        return command_push(args) if args.command == "push" else command_run(args)
    except (lsw_link.LinkError, subprocess.SubprocessError, OSError) as error:
        print(f"render-run-guest: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
