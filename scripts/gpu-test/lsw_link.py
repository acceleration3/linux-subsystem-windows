"""The LSW VM from the host, without the manager: the guest agent's control
link (docs/link-protocol.md) and the guest's C$ share.

The link is one TCP connection to the agent's port, forwarded by the domain's
hostfwd rule; every message is ``[type:u8][len:u32 BE][JSON]``. Requests are
answered by id and events are dropped: the agent hands an event to whichever
connection drains it first, and the manager may hold another one, so nothing
here waits on an event. A job's end is read off a file the job writes.

Files go through smbclient on the forwarded SMB port as the VM's own account
(lsw/lsw unless the domain was installed with another).
"""
import json
import os
import pathlib
import socket
import struct
import subprocess
import tempfile
import threading
import time

AGENT_PORT = 5102
SMB_PORT = 5445
CONTROL = 1
CALL_TIMEOUT = 60.0


class LinkError(Exception):
    """The agent or the share did not do what was asked."""


class GuestGone(LinkError):
    """Nothing answers on the forwarded port: the VM is down."""


GONE = ("NT_STATUS_CONNECTION_REFUSED", "NT_STATUS_CONNECTION_RESET", "NT_STATUS_IO_TIMEOUT",
        "NT_STATUS_HOST_UNREACHABLE")


class AgentLink:
    """One connection to the guest agent: requests and their replies."""

    def __init__(self, host="127.0.0.1", port=AGENT_PORT, timeout=CALL_TIMEOUT):
        self.timeout = timeout
        self.socket = socket.create_connection((host, port), timeout=timeout)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buffer = bytearray()
        self.next_id = 1
        self.lock = threading.Lock()

    def close(self):
        self.socket.close()

    def receive(self, deadline):
        while True:
            if len(self.buffer) >= 5:
                kind, length = struct.unpack_from(">BI", self.buffer)
                if len(self.buffer) >= 5 + length:
                    payload = bytes(self.buffer[5:5 + length])
                    del self.buffer[:5 + length]
                    if kind == CONTROL:
                        return json.loads(payload)
                    continue
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise LinkError("the agent did not answer in time")
            self.socket.settimeout(remaining)
            try:
                data = self.socket.recv(1 << 20)
            except socket.timeout as error:
                raise LinkError("the agent did not answer in time") from error
            except OSError as error:
                raise LinkError(f"the link failed: {error}") from error
            if not data:
                raise LinkError("the agent closed the link")
            self.buffer += data

    def call(self, command, params=None, timeout=None):
        """The command's result; an error reply, or a result saying ok=false, raises."""
        with self.lock:
            request_id = self.next_id
            self.next_id += 1
            body = json.dumps({"id": request_id, "cmd": command, "params": params or {}}).encode()
            try:
                self.socket.sendall(struct.pack(">BI", CONTROL, len(body)) + body)
            except OSError as error:
                raise LinkError(f"the link failed: {error}") from error
            deadline = time.monotonic() + (timeout or self.timeout)
            while True:
                message = self.receive(deadline)
                if message.get("id") != request_id:
                    continue
                if "error" in message:
                    raise LinkError(f"{command}: {message['error']}")
                result = message.get("result", {})
                if isinstance(result, dict) and (result.get("ok") is False or result.get("error")):
                    raise LinkError(f"{command}: {result.get('error') or 'refused'}")
                return result


def wait_for_agent(deadline, port=AGENT_PORT):
    """A link to an agent that answers system_info, or LinkError at the deadline.

    QEMU's hostfwd accepts a connection whether or not the guest listens, so
    only an answer proves the agent is up - and it listens only once the
    user's desktop is usable."""
    last = None
    while time.monotonic() < deadline:
        link = None
        try:
            link = AgentLink(port=port, timeout=10.0)
            link.call("system_info")
            link.timeout = CALL_TIMEOUT
            return link
        except (OSError, LinkError, ValueError) as error:
            last = error
            if link is not None:
                link.close()
        time.sleep(5.0)
    raise LinkError(f"the guest agent never answered: {last}")


class GuestShare:
    """The guest's C: drive over SMB: ``C:\\path`` in, local files out."""

    def __init__(self, user="lsw", password="lsw", port=SMB_PORT):
        self.user = user
        self.password = password
        self.port = port

    @staticmethod
    def share_path(guest_path):
        if len(guest_path) < 3 or guest_path[1:3] != ":\\" or guest_path[0].upper() != "C":
            raise LinkError(f"{guest_path}: only C:\\ is reachable over the share")
        return guest_path[2:]

    def smbclient(self, commands, timeout=600):
        environment = dict(os.environ, PASSWD=self.password)
        done = subprocess.run(["smbclient", "//127.0.0.1/C$", "-p", str(self.port), "-U", self.user,
                               "-m", "SMB3", "-c", commands], capture_output=True, text=True,
                              env=environment, timeout=timeout)
        out = done.stdout + done.stderr
        if any(status in out for status in GONE):
            raise GuestGone(f"the guest's share does not answer: {out.strip()[-200:]}")
        return done.returncode, out

    def put(self, local, guest_path, timeout=3600):
        code, out = self.smbclient(f'put "{local}" "{self.share_path(guest_path)}"', timeout)
        if code != 0 or "NT_STATUS" in out:
            raise LinkError(f"put {guest_path}: {out.strip()[-300:]}")

    def put_bytes(self, data, guest_path):
        with tempfile.NamedTemporaryFile() as staged:
            staged.write(data)
            staged.flush()
            self.put(staged.name, guest_path)

    def get(self, guest_path, local, timeout=3600):
        local = pathlib.Path(local)
        local.parent.mkdir(parents=True, exist_ok=True)
        code, out = self.smbclient(f'get "{self.share_path(guest_path)}" "{local}"', timeout)
        if code != 0 or "NT_STATUS" in out:
            local.unlink(missing_ok=True)
            raise LinkError(f"get {guest_path}: {out.strip()[-300:]}")

    def get_bytes(self, guest_path):
        with tempfile.TemporaryDirectory() as directory:
            local = pathlib.Path(directory) / "file"
            self.get(guest_path, local)
            return local.read_bytes()

    def exists(self, guest_path):
        code, out = self.smbclient(f'allinfo "{self.share_path(guest_path)}"', 60)
        return code == 0 and "NT_STATUS" not in out

    def make_directories(self, guest_directory):
        """Every level of a C:\\ directory; levels that exist already are fine."""
        parts = [part for part in self.share_path(guest_directory).split("\\") if part]
        commands = ";".join(f'mkdir "\\{chr(92).join(parts[:depth])}"' for depth in range(1, len(parts) + 1))
        self.smbclient(commands, 60)
        if not self.exists(guest_directory):
            raise LinkError(f"could not make {guest_directory}")

    def remove(self, guest_path):
        self.smbclient(f'del "{self.share_path(guest_path)}"', 60)
