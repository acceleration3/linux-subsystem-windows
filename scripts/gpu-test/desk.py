#!/usr/bin/env python3
"""The running LSW VM's desktop, driven by hand or by a script: an elevated
helper in the console session (guest/desk-server.ps1) that runs PowerShell
snippets, sends input and takes screenshots. Used to record traces of
applications that run elevated (Resolve under apitrace's wrapper,
CLIntercept's system mode): UIPI drops input a non-elevated sender sends to
them, and the desktop task Desktop.run uses is not elevated.

  desk.py start                     the helper, from a /rl highest console task
  desk.py stop
  desk.py ps 'SNIPPET' ...          snippets run by the helper, their output printed
                                    (helpers: Click X Y [-Right], DClick X Y, Drag X1 Y1 X2 Y2,
                                    Wheel X Y N, Press 'ctrl+s' [MS] [N], Type-Text 'abc',
                                    Focus NAME, Windows, Shot PATH [X Y W H] [SCALE])
  desk.py shot OUT.png [X Y W H] [--scale S]   the screen (or a part) into OUT.png
  desk.py put LOCAL GUEST / get GUEST LOCAL      files over the share
  desk.py resolve-lua 'CODE'        CODE typed into DaVinci Resolve's console (opened with F6 if it
                                    is not), run, the console closed again: Resolve's free edition
                                    refuses external scripting, and its console's Lua cannot write
                                    files, so what it prints is read off a screenshot

Each command talks to the VM on this machine (lsw-manager's, or the one
gpu_matrix.py hold keeps) through its C$ share; `start` also uses the agent.
"""
import argparse
import itertools
import os
import pathlib
import shutil
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
AV = HERE.parents[1] / "external" / "accel-virt"
sys.path[:0] = [str(HERE), str(AV / "tools"), str(AV / "tools" / "guest"), str(AV / "tools" / "host")]

from lsw_link import GuestShare, LinkError  # noqa: E402

APPS = "C:\\av\\apps"
DESK = APPS + "\\desk"
TASK = "avdesk"
SERVER_CMD = APPS + "\\desk-server.cmd"


class Desk:
    def __init__(self, user="lsw", password="lsw"):
        self.share = GuestShare(user, password)
        self.counter = itertools.count(int(time.time() * 1000) % 10**9)

    def run(self, snippet, timeout=120):
        name = f"{next(self.counter):010d}"
        path = self.share.share_path(f"{DESK}\\{name}")
        with tempfile.NamedTemporaryFile() as staged:
            staged.write(snippet.encode("utf-8-sig"))
            staged.flush()
            code, out = self.share.smbclient(f'put "{staged.name}" "{path}.txt"; rename "{path}.txt" "{path}.ps1"', 60)
        if code != 0 or "NT_STATUS" in out:
            raise LinkError(f"sending the snippet: {out.strip()[-300:]}")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            time.sleep(0.25)
            try:
                text = self.share.get_bytes(f"{DESK}\\{name}.out")
            except LinkError:
                continue
            self.share.remove(f"{DESK}\\{name}.out")
            return text.decode("utf-8-sig", errors="replace")
        raise LinkError(f"the helper did not answer in {timeout} s (is it started? desk.py start)")

    def guest(self):
        """The agent's link (the held VM's bridge when gpu_matrix.py hold serves one), and its work directory."""
        import gpu_matrix
        work = pathlib.Path(tempfile.mkdtemp(prefix="lsw-desk-"))
        guest = gpu_matrix.Guest(work, self.share)
        held = gpu_matrix.bridge_directory("hold") / "agent.sock"
        if held.exists():
            guest.agent = gpu_matrix.wait_for_bridge(str(held), time.monotonic() + 30, 600.0)
        else:
            guest.connect()
        return guest, work

    def start(self):
        import run_guest_suite as suite
        guest, work = self.guest()
        try:
            self.share.make_directories(DESK)
            self.share.remove(f"{DESK}\\server.ready")
            for asset in ("desk-server.ps1", "hidden.vbs"):
                self.share.put(HERE / "guest" / asset, f"{APPS}\\{asset}")
            self.share.put_bytes(f"@echo off\r\npowershell -NoProfile -ExecutionPolicy Bypass -File "
                                 f"{APPS}\\desk-server.ps1 -Dir {DESK} > {APPS}\\desk-server.log 2>&1\r\n".encode(),
                                 SERVER_CMD)
            code, text = guest.powershell("(Get-CimInstance Win32_ComputerSystem).UserName", 120)
            user = text.strip().splitlines()[-1].strip() if text.strip() else ""
            if not user:
                raise SystemExit("no one is logged into the console session")
            for arguments in (["/create", "/tn", TASK, "/tr", f"wscript.exe //B {APPS}\\hidden.vbs {SERVER_CMD}",
                               "/sc", "once", "/st", "23:59", "/ru", user, "/it", "/rl", "highest", "/f"],
                              ["/run", "/tn", TASK]):
                code, text = guest.run(suite.SCHTASKS, arguments, 120)
                if code != 0:
                    raise SystemExit(f"schtasks {arguments[0]}: exit {code}: {text.strip()[-300:]}")
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                try:
                    print("helper:", self.share.get_bytes(f"{DESK}\\server.ready").decode(errors="replace"))
                    return
                except LinkError:
                    time.sleep(1)
            raise SystemExit("the helper did not start (see C:\\av\\apps\\desk-server.log)")
        finally:
            guest.close()
            shutil.rmtree(work, ignore_errors=True)

    def stop(self):
        self.share.put_bytes(b"stop", f"{DESK}\\stop")
        import run_guest_suite as suite
        guest, work = self.guest()
        try:
            guest.run(suite.SCHTASKS, ["/delete", "/tn", TASK, "/f"], 120)
        finally:
            guest.close()
            shutil.rmtree(work, ignore_errors=True)

    def shot(self, local, rect=(0, 0, 0, 0), scale=1.0):
        guest_png = f"{DESK}\\shot.png"
        out = self.run(f"Shot '{guest_png}' {rect[0]} {rect[1]} {rect[2]} {rect[3]} {scale}")
        self.share.get(guest_png, local)
        return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--user", default="lsw")
    parser.add_argument("--password", default="lsw")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("start")
    commands.add_parser("stop")
    ps = commands.add_parser("ps")
    ps.add_argument("snippets", nargs="+")
    ps.add_argument("--timeout", type=int, default=120)
    shot = commands.add_parser("shot")
    shot.add_argument("out")
    shot.add_argument("rect", nargs="*", type=int)
    shot.add_argument("--scale", type=float, default=1.0)
    put = commands.add_parser("put")
    put.add_argument("local")
    put.add_argument("guest")
    lua = commands.add_parser("resolve-lua")
    lua.add_argument("code")
    lua.add_argument("--keep-open", action="store_true", help="leave the console open (to read what it printed)")
    get = commands.add_parser("get")
    get.add_argument("guest")
    get.add_argument("local")
    args = parser.parse_args()
    desk = Desk(args.user, args.password)
    if args.command == "start":
        desk.start()
    elif args.command == "stop":
        desk.stop()
    elif args.command == "ps":
        for snippet in args.snippets:
            sys.stdout.write(desk.run(snippet, args.timeout))
    elif args.command == "shot":
        print(desk.shot(args.out, tuple(args.rect) if len(args.rect) == 4 else (0, 0, 0, 0), args.scale).strip())
    elif args.command == "put":
        desk.share.put(args.local, args.guest)
    elif args.command == "resolve-lua":
        quoted = args.code.replace("'", "''")
        sys.stdout.write(desk.run(f"Resolve-Lua '{quoted}' {'$true' if args.keep_open else '$false'}", 300))
    elif args.command == "get":
        desk.share.get(args.guest, args.local)
    return 0


if __name__ == "__main__":
    sys.exit(main())
