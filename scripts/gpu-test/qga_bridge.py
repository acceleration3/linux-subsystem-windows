import base64
import itertools
import json
import os
import pathlib
import socketserver
import subprocess
import tempfile
import threading
import time

from lsw_link import GuestGone, LinkError

EXEC_DIRECTORY = "C:\\av\\qga"
DELIMITER = b"\xff"
STATUS_INTERVAL = 1.0
LAUNCHER = r"""$ErrorActionPreference = 'Stop'
$dir = '{directory}'
$id = '{id}'
$code = -1
try {{
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{path}'))
    $psi.Arguments = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('{arguments}'))
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($psi)
    $process.StandardInput.Close()
    $out = [IO.File]::Create("$dir\$id.out")
    $err = [IO.File]::Create("$dir\$id.err")
    $copies = @($process.StandardOutput.BaseStream.CopyToAsync($out),
                $process.StandardError.BaseStream.CopyToAsync($err))
    $process.WaitForExit()
    [Threading.Tasks.Task]::WaitAll($copies)
    $out.Close()
    $err.Close()
    $code = $process.ExitCode
}} catch {{
    [IO.File]::WriteAllText("$dir\$id.err", $_.ToString())
}}
[IO.File]::WriteAllText("$dir\$id.rc.tmp", "$code")
Move-Item -Force "$dir\$id.rc.tmp" "$dir\$id.rc"
"""


def command_line(arguments):
    return subprocess.list2cmdline(arguments)


class Guest:
    def __init__(self, link, share):
        self.link = link
        self.share = share
        self.ids = itertools.count(1)
        self.prefix = f"{os.getpid()}-{int(time.time())}"
        self.processes = {}
        self.files = {}
        self.lock = threading.Lock()
        self.share.make_directories(EXEC_DIRECTORY)

    def exec(self, path, arguments):
        pid = next(self.ids)
        job = f"{self.prefix}-{pid}"
        launcher = LAUNCHER.format(directory=EXEC_DIRECTORY, id=job,
                                   path=base64.b64encode(path.encode()).decode(),
                                   arguments=base64.b64encode(command_line(arguments).encode()).decode())
        script = f"{EXEC_DIRECTORY}\\{job}.ps1"
        self.share.put_bytes(launcher.encode("utf-8-sig"), script)
        self.link.call("run_script", {"name": f"qga {job}",
                                      "script": "@echo off\r\npowershell -NoProfile -NonInteractive "
                                                f"-ExecutionPolicy Bypass -File {script}\r\n"})
        with self.lock:
            self.processes[pid] = {"job": job, "checked": 0.0, "done": None}
        return pid

    def status(self, pid):
        with self.lock:
            process = self.processes.get(pid)
        if process is None:
            raise LinkError(f"no process {pid}")
        if process["done"] is not None:
            return process["done"]
        if time.monotonic() - process["checked"] < STATUS_INTERVAL:
            return {"exited": False}
        process["checked"] = time.monotonic()
        job = process["job"]
        try:
            code = self.share.get_bytes(f"{EXEC_DIRECTORY}\\{job}.rc")
        except GuestGone:
            raise
        except LinkError:
            return {"exited": False}
        done = {"exited": True, "exitcode": int(code.decode(errors="replace").strip() or "-1")}
        for stream, field in (("out", "out-data"), ("err", "err-data")):
            try:
                done[field] = base64.b64encode(self.share.get_bytes(f"{EXEC_DIRECTORY}\\{job}.{stream}")).decode()
            except LinkError:
                done[field] = ""
        for suffix in ("ps1", "rc", "out", "err"):
            self.share.remove(f"{EXEC_DIRECTORY}\\{job}.{suffix}")
        process["done"] = done
        return done

    def file_open(self, path, mode):
        handle = next(self.ids)
        staged = tempfile.NamedTemporaryFile(delete=False)
        staged.close()
        entry = {"path": path, "local": pathlib.Path(staged.name), "offset": 0, "writing": "w" in mode or "a" in mode}
        if not entry["writing"]:
            try:
                self.share.get(path, entry["local"])
            except LinkError:
                entry["local"].unlink(missing_ok=True)
                raise
        else:
            entry["local"].write_bytes(b"")
        with self.lock:
            self.files[handle] = entry
        return handle

    def file_entry(self, handle):
        with self.lock:
            entry = self.files.get(handle)
        if entry is None:
            raise LinkError(f"no open file {handle}")
        return entry

    def file_read(self, handle, count):
        entry = self.file_entry(handle)
        with open(entry["local"], "rb") as source:
            source.seek(entry["offset"])
            data = source.read(count)
        entry["offset"] += len(data)
        return {"count": len(data), "buf-b64": base64.b64encode(data).decode(), "eof": len(data) < count}

    def file_write(self, handle, data):
        entry = self.file_entry(handle)
        with open(entry["local"], "ab") as target:
            target.write(data)
        return {"count": len(data), "eof": False}

    def file_close(self, handle):
        entry = self.file_entry(handle)
        with self.lock:
            del self.files[handle]
        try:
            if entry["writing"]:
                self.share.put(entry["local"], entry["path"])
        finally:
            entry["local"].unlink(missing_ok=True)
        return {}

    def answer(self, request):
        command = request.get("execute")
        arguments = request.get("arguments") or {}
        if command in ("guest-sync", "guest-sync-delimited"):
            return arguments.get("id")
        if command == "guest-ping":
            return {}
        if command == "guest-info":
            return {"version": "lsw-bridge", "supported_commands": []}
        if command == "guest-exec":
            if arguments.get("input-data"):
                raise LinkError("guest-exec: input-data is not supported")
            return {"pid": self.exec(arguments["path"], arguments.get("arg", []))}
        if command == "guest-exec-status":
            return self.status(arguments["pid"])
        if command == "guest-file-open":
            return self.file_open(arguments["path"], arguments.get("mode", "r"))
        if command == "guest-file-read":
            return self.file_read(arguments["handle"], arguments.get("count", 4096))
        if command == "guest-file-write":
            return self.file_write(arguments["handle"], base64.b64decode(arguments["buf-b64"]))
        if command == "guest-file-close":
            return self.file_close(arguments["handle"])
        raise LinkError(f"{command} is not supported by the LSW bridge")


class Connection(socketserver.StreamRequestHandler):
    def handle(self):
        guest = self.server.guest
        while True:
            line = self.rfile.readline()
            if not line:
                return
            delimited = line.startswith(DELIMITER)
            request = {}
            try:
                request = json.loads(line.lstrip(DELIMITER))
                reply = {"return": guest.answer(request)}
            except (LinkError, KeyError, ValueError, TypeError, AttributeError, OSError,
                    subprocess.SubprocessError) as error:
                request = request if isinstance(request, dict) else {}
                reply = {"error": {"class": "GenericError", "desc": str(error)}}
            prefix = DELIMITER if delimited and request.get("execute") == "guest-sync-delimited" else b""
            self.wfile.write(prefix + json.dumps(reply).encode() + b"\n")
            self.wfile.flush()


class Server(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True


class Bridge:
    def __init__(self, work, link, share):
        self.path = pathlib.Path(work) / "agent.sock"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.unlink(missing_ok=True)
        self.server = Server(str(self.path), Connection)
        self.server.guest = Guest(link, share)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def start(self):
        self.thread.start()
        return self

    def stop(self):
        self.server.shutdown()
        self.server.server_close()
        self.path.unlink(missing_ok=True)
