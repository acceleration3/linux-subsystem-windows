#!/usr/bin/env python3
"""A first-come first-served queue for the use of one VM, shared by every agent on this machine.

  vm_queue.py hold --host HOST [--queue-dir DIR] [--who TEXT] [--poll S] -- COMMAND...
      takes a ticket, waits until it is the oldest live one, runs COMMAND, and gives the ticket back when COMMAND ends,
      when this process is signalled, or when it exits for any other reason; the exit status is COMMAND's
  vm_queue.py status --host HOST [--queue-dir DIR]
      prints the tickets in order, the first being the holder

A ticket is a file <enqueue time in ns>-<pid> in <queue dir>/<host>/. A ticket whose process is gone (or whose pid
now belongs to another process) is removed by whoever looks at the queue, so a killed holder never blocks it.
"""
import argparse
import json
import os
import pathlib
import signal
import subprocess
import sys
import time


def default_queue_dir():
    return pathlib.Path(os.environ.get("LSW_VERIFY_QUEUE") or (os.environ.get("XDG_RUNTIME_DIR") or "/tmp") + "/lsw-verify-queue")


def start_time(pid):
    try:
        text = pathlib.Path(f"/proc/{pid}/stat").read_text()
    except OSError:
        return None
    return text.rsplit(")", 1)[1].split()[19]


class Ticket:
    def __init__(self, path, data):
        self.path = path
        self.data = data

    @property
    def pid(self):
        return self.data["pid"]

    def alive(self):
        return start_time(self.pid) == self.data["start"]


class Queue:
    def __init__(self, root, host):
        self.dir = pathlib.Path(root) / host
        self.dir.mkdir(parents=True, exist_ok=True)

    def join(self, who="", pid=None):
        pid = pid or os.getpid()
        name = f"{time.time_ns():020d}-{pid}"
        data = {"pid": pid, "start": start_time(pid), "who": who, "since": time.time()}
        temp = self.dir / f".{name}.tmp"
        temp.write_text(json.dumps(data))
        path = self.dir / name
        os.replace(temp, path)
        return Ticket(path, data)

    def tickets(self):
        found = []
        for path in sorted(self.dir.glob("[0-9]*-*")):
            try:
                found.append(Ticket(path, json.loads(path.read_text())))
            except (OSError, ValueError):
                continue
        return found

    def live(self):
        live = []
        for ticket in self.tickets():
            if ticket.alive():
                live.append(ticket)
            else:
                ticket.path.unlink(missing_ok=True)
        return live

    def position(self, ticket):
        for index, other in enumerate(self.live()):
            if other.path == ticket.path:
                return index
        return None

    def leave(self, ticket):
        ticket.path.unlink(missing_ok=True)

    def wait(self, ticket, poll=2.0, report=None):
        last = None
        while True:
            position = self.position(ticket)
            if position is None:
                raise RuntimeError("the ticket was removed")
            if position == 0:
                return
            if report and (last is None or time.monotonic() - last >= 60):
                holder = self.live()[0]
                report(position, holder)
                last = time.monotonic()
            time.sleep(poll)


def hold(args):
    queue = Queue(args.queue_dir, args.host)
    ticket = queue.join(args.who)

    def report(position, holder):
        held = int(time.time() - holder.data["since"])
        print(f"vm_queue: {args.host}: {position} ahead of you; the oldest is pid {holder.pid} "
              f"({holder.data.get('who') or '?'}), queued {held // 60} min ago", file=sys.stderr, flush=True)

    child = None

    def stop(number, frame):
        if child and child.poll() is None:
            child.send_signal(number)
        else:
            queue.leave(ticket)
            sys.exit(128 + number)

    for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, stop)
    try:
        queue.wait(ticket, args.poll, report)
        print(f"vm_queue: {args.host}: your turn", file=sys.stderr, flush=True)
        child = subprocess.Popen(args.command)
        return child.wait()
    finally:
        queue.leave(ticket)


def status(args):
    queue = Queue(args.queue_dir, args.host)
    for index, ticket in enumerate(queue.live()):
        age = int(time.time() - ticket.data["since"])
        print(f"{'holder' if index == 0 else index:>6}  pid {ticket.pid}  {age // 60} min  {ticket.data.get('who', '')}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="action", required=True)
    for name in ("hold", "status"):
        sub = commands.add_parser(name)
        sub.add_argument("--host", required=True)
        sub.add_argument("--queue-dir", default=default_queue_dir())
        if name == "hold":
            sub.add_argument("--who", default="")
            sub.add_argument("--poll", type=float, default=2.0)
            sub.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.action == "hold":
        if args.command[:1] == ["--"]:
            args.command = args.command[1:]
        if not args.command:
            parser.error("hold needs a command after --")
        return hold(args)
    return status(args)


if __name__ == "__main__":
    sys.exit(main())
