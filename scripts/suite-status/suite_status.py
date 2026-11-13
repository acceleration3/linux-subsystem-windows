#!/usr/bin/env python3
import argparse
import collections
import concurrent.futures
import datetime
import ipaddress
import json
import os
import socket
import subprocess
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
REASON_CHARS = 400
FAILURES_KEPT = 600
RATE_WINDOW = 300.0
BODY_LIMIT = 64 << 20


def clock_text(moment):
    return datetime.datetime.fromtimestamp(moment, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def api_of(check_id):
    pieces = str(check_id).split(".")
    return pieces[1] if len(pieces) > 1 else "?"


class Invocation:
    def __init__(self, key, begin, first_report, resends):
        self.key = key
        self.label = str(begin.get("label") or "")
        self.exe = str(begin.get("exe") or "")
        self.bits = begin.get("bitness")
        self.suites = [str(name) for name in begin.get("suites") or []]
        self.adapter = begin.get("adapter") if isinstance(begin.get("adapter"), dict) else {}
        self.started = str(begin.get("started") or "")
        self.phase = str(begin.get("phase") or "")
        self.jobs = begin.get("jobs")
        self.planned = [str(check_id) for check_id in begin.get("planned") or []]
        self.planned_apis = collections.Counter(api_of(check_id) for check_id in self.planned)
        self.first_report = first_report
        self.last_report = first_report
        self.resends = resends
        self.seq = 0
        self.running = {}
        self.counts = collections.Counter()
        self.done_apis = collections.Counter()
        self.bad_apis = collections.Counter()
        self.failures = []
        self.failures_cut = 0
        self.finish_times = collections.deque()
        self.end = None
        self.totals = {}

    def apply(self, events, receipt):
        for event in events:
            if not isinstance(event, dict):
                continue
            kind = event.get("t")
            ago = event.get("ago_ms")
            moment = receipt - (ago / 1000.0 if isinstance(ago, (int, float)) and ago > 0 else 0.0)
            check_id = str(event.get("id") or "")
            if kind == "start":
                self.running[(check_id, event.get("pid"))] = moment
            elif kind == "finish":
                self.running.pop((check_id, event.get("pid")), None)
                state = str(event.get("status") or "?")
                api = api_of(check_id)
                self.counts[state] += 1
                self.done_apis[api] += 1
                self.finish_times.append(moment)
                if state not in ("ok", "skip"):
                    self.bad_apis[api] += 1
                    if len(self.failures) < FAILURES_KEPT:
                        self.failures.append({"id": check_id, "status": state, "ms": event.get("ms"),
                                              "reason": str(event.get("reason") or "")[:REASON_CHARS]})
                    else:
                        self.failures_cut += 1
            elif kind == "end":
                self.running.clear()
                self.end = {"time": clock_text(moment), "complete": bool(event.get("complete")),
                            "failed": event.get("failed"), "crashed": event.get("crashed"), "timed_out": event.get("timed_out")}

    def done(self):
        return sum(self.counts.values())

    def state(self, now, silent_after):
        if self.end is not None:
            return "finished"
        return "silent" if now - self.last_report > silent_after else "running"

    def rate(self, now):
        while self.finish_times and now - self.finish_times[0] > RATE_WINDOW:
            self.finish_times.popleft()
        span = min(RATE_WINDOW, now - self.first_report)
        if span < 1.0:
            return None
        return round(len(self.finish_times) * 60.0 / span, 1)

    def view(self, now, silent_after):
        state = self.state(now, silent_after)
        apis = {api: {"planned": self.planned_apis.get(api, 0), "done": self.done_apis.get(api, 0), "bad": self.bad_apis.get(api, 0)}
                for api in sorted(set(self.planned_apis) | set(self.done_apis))}
        running = sorted(({"id": check_id, "pid": pid, "seconds": max(0, int(now - since))}
                          for (check_id, pid), since in self.running.items()), key=lambda entry: -entry["seconds"])
        return {"invocation": self.key, "label": self.label, "exe": self.exe, "bits": self.bits, "suites": self.suites,
                "phase": self.phase, "jobs": self.jobs, "started": self.started, "state": state,
                "first_report": clock_text(self.first_report), "last_report": clock_text(self.last_report),
                "seconds_since_report": max(0, int(now - self.last_report)),
                "silent_since": clock_text(self.last_report) if state == "silent" else None,
                "planned": len(self.planned), "done": self.done(), "counts": dict(self.counts),
                "apis": apis, "running": running, "checks_per_minute": self.rate(now), "failures": self.failures,
                "failures_cut": self.failures_cut, "end": self.end, "resends": self.resends,
                "reporter_dropped": self.totals.get("dropped")}


class Part:
    def __init__(self, key):
        self.key = key
        self.invocations = {}

    def view(self, now, silent_after):
        shown = [invocation.view(now, silent_after) for invocation in self.invocations.values()]
        latest = list(self.invocations.values())[-1]
        return {"part": self.key, "adapter": str(latest.adapter.get("name") or ""), "adapter_identity": latest.adapter,
                "bits": latest.bits, "state": shown[-1]["state"], "silent_since": shown[-1]["silent_since"],
                "last_report": shown[-1]["last_report"], "invocations": shown}


class Run:
    def __init__(self, label, now):
        self.label = label
        self.first_report = now
        self.last_report = now
        self.parts = {}

    def view(self, now, silent_after):
        parts = [self.parts[key].view(now, silent_after) for key in sorted(self.parts)]
        states = {part["state"] for part in parts}
        state = "running" if "running" in states else "silent" if "silent" in states else "finished"
        return {"run": self.label, "state": state, "first_report": clock_text(self.first_report),
                "last_report": clock_text(self.last_report), "parts": parts}


class Store:
    def __init__(self, name, silent_after=30.0, runs_kept=6, clock=time.time):
        self.name = name
        self.silent_after = silent_after
        self.runs_kept = runs_kept
        self.clock = clock
        self.lock = threading.Lock()
        self.runs = {}
        self.reports = 0
        self.last_report = None

    def report(self, batch):
        if not isinstance(batch, dict):
            return 400, {"error": "the report is not an object"}
        keys = [batch.get(name) for name in ("run", "part", "invocation")]
        events = batch.get("events")
        seq = batch.get("seq")
        if not all(isinstance(key, str) and key for key in keys) or not isinstance(events, list) or not isinstance(seq, int):
            return 400, {"error": "the report needs run, part, invocation, seq and events"}
        run_label, part_key, invocation_key = keys
        begin = batch.get("begin")
        totals = batch.get("totals") if isinstance(batch.get("totals"), dict) else {}
        now = self.clock()
        with self.lock:
            self.reports += 1
            self.last_report = now
            run = self.runs.get(run_label)
            part = run.parts.get(part_key) if run else None
            known = part.invocations.get(invocation_key) if part else None
            if isinstance(begin, dict):
                if run is None:
                    run = self.runs[run_label] = Run(run_label, now)
                    self.forget_old_runs()
                if part is None:
                    part = run.parts[part_key] = Part(part_key)
                invocation = Invocation(invocation_key, begin, known.first_report if known else now, known.resends + 1 if known else 0)
                part.invocations[invocation_key] = invocation
            elif known is None:
                return 409, {"error": "unknown part: send the begin record"}
            elif seq != known.seq + 1:
                return 409, {"error": f"a report is missing before number {seq}: send the begin record"}
            else:
                invocation = known
            invocation.apply(events, now)
            invocation.seq = seq
            invocation.totals = totals
            invocation.last_report = now
            run.last_report = now
            if isinstance(totals.get("done"), int) and totals["done"] != invocation.done():
                invocation.seq = -1
                return 409, {"error": "the totals do not match the results received: send the begin record"}
        return 200, {"ok": True}

    def forget_old_runs(self):
        for label in sorted(self.runs, key=lambda name: self.runs[name].last_report, reverse=True)[self.runs_kept:]:
            del self.runs[label]

    def status(self):
        now = self.clock()
        with self.lock:
            runs = sorted(self.runs.values(), key=lambda run: run.first_report, reverse=True)
            return {"machine": self.name, "time": clock_text(now), "silent_after": self.silent_after, "reports": self.reports,
                    "last_report": clock_text(self.last_report) if self.last_report is not None else None,
                    "runs": [run.view(now, self.silent_after) for run in runs]}


class Peer:
    def __init__(self, name, url, forward=None):
        self.name = name
        self.url = url
        self.forward = forward

    def open_forward(self):
        control_path, target, local_port, port = self.forward
        try:
            with socket.create_connection(("127.0.0.1", local_port), timeout=1):
                return None
        except OSError:
            pass
        base = ["ssh", "-o", f"ControlPath={control_path}", "-O"]
        try:
            if subprocess.run(base + ["check", target], capture_output=True, stdin=subprocess.DEVNULL, timeout=5).returncode != 0:
                return "no open ssh master to this machine"
            subprocess.run(base + ["forward", "-L", f"127.0.0.1:{local_port}:127.0.0.1:{port}", target],
                           capture_output=True, stdin=subprocess.DEVNULL, timeout=5)
        except (OSError, subprocess.TimeoutExpired) as error:
            return f"the ssh forward failed: {type(error).__name__}"
        return None

    def read(self):
        problem = self.open_forward() if self.forward else None
        if problem:
            return {"name": self.name, "url": self.url, "error": problem}
        try:
            with urllib.request.urlopen(self.url, timeout=4) as answer:
                return {"name": self.name, "url": self.url, "status": json.loads(answer.read().decode("utf-8"))}
        except Exception as error:
            return {"name": self.name, "url": self.url, "error": f"{type(error).__name__}: {error}"[:200]}


class Peers:
    def __init__(self, peers):
        self.peers = peers
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=max(1, len(peers)))

    def gather(self):
        return [future.result() for future in [self.pool.submit(peer.read) for peer in self.peers]]


def host_lines(path):
    try:
        with open(path, encoding="utf-8") as hosts:
            lines = hosts.read().splitlines()
    except OSError:
        return []
    return [fields[:2] for fields in (line.split() for line in lines) if len(fields) >= 2 and not fields[0].startswith("#")]


def own_name(path):
    return next((name for name, target in host_lines(path) if target == "local"), "local")


def peers_from_hosts(path, port, tunnel, control_path):
    peers = []
    local_port = port
    for name, target in host_lines(path):
        if target == "local":
            continue
        if tunnel:
            local_port += 1
            peers.append(Peer(name, f"http://127.0.0.1:{local_port}/status", (control_path, target, local_port, port)))
        else:
            peers.append(Peer(name, f"http://{target.rsplit('@', 1)[-1]}:{port}/status"))
    return peers


def make_handler(store, peers):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            return

        def send(self, code, kind, body):
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def send_json(self, code, data):
            self.send(code, "application/json", json.dumps(data).encode("utf-8"))

        def from_loopback(self):
            try:
                address = ipaddress.ip_address(self.client_address[0])
            except ValueError:
                return False
            return (getattr(address, "ipv4_mapped", None) or address).is_loopback

        def do_POST(self):
            try:
                length = int(self.headers.get("Content-Length") or -1)
            except ValueError:
                length = -1
            if length < 0 or length > BODY_LIMIT:
                self.close_connection = True
                self.send_json(411 if length < 0 else 413, {"error": "the report needs a Content-Length within the limit"})
                return
            body = self.rfile.read(length)
            if self.path.split("?", 1)[0] != "/report":
                self.send_json(404, {"error": "not found"})
            elif not self.from_loopback():
                self.send_json(403, {"error": "reports are accepted from this machine's VM only"})
            else:
                try:
                    batch = json.loads(body.decode("utf-8", errors="replace"))
                except ValueError:
                    self.send_json(400, {"error": "the report is not JSON"})
                    return
                self.send_json(*store.report(batch))

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/status":
                self.send_json(200, store.status())
            elif path == "/api/machines":
                machines = [{"name": store.name, "url": "", "status": store.status()}] + peers.gather()
                self.send_json(200, {"time": clock_text(store.clock()), "machines": machines})
            elif path in ("/", "/index.html"):
                try:
                    with open(os.path.join(HERE, "index.html"), "rb") as page:
                        self.send(200, "text/html; charset=utf-8", page.read())
                except OSError:
                    self.send(404, "text/plain", b"index.html is missing beside the server\n")
            else:
                self.send(404, "text/plain", b"not found\n")

    return Handler


def make_server(bind, port, store, peers):
    return ThreadingHTTPServer((bind, port), make_handler(store, peers))


def serve(args):
    peers = [Peer(*item.split("=", 1)) for item in args.peer if "=" in item]
    if not args.no_hosts:
        named = {peer.name for peer in peers}
        peers += [peer for peer in peers_from_hosts(args.hosts_file, args.port, args.tunnel, args.control_path) if peer.name not in named]
    store = Store(args.name or own_name(args.hosts_file), args.silent_after, args.runs)
    server = make_server(args.bind, args.port, store, Peers(peers))
    print(f"suite status of {store.name} on http://{args.bind}:{args.port}/ ({len(peers)} other machines)", flush=True)
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description="what the suite executables of this machine's VM report, as JSON and as a page")
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("serve", help="receive the reports and serve the status and the page")
    command.add_argument("--port", type=int, default=5190)
    command.add_argument("--bind", default="127.0.0.1", help="the address to listen on (default: this machine only)")
    command.add_argument("--name", default="", help="this machine's name on the page (default: the local line of the hosts file)")
    command.add_argument("--peer", action="append", default=[], metavar="NAME=URL", help="another machine's /status")
    command.add_argument("--hosts-file", default=os.path.join(HERE, "..", "gpu-test", "hosts"))
    command.add_argument("--no-hosts", action="store_true", help="do not take the other machines from the hosts file")
    command.add_argument("--tunnel", action="store_true", help="reach the hosts file's machines through their open ssh masters")
    command.add_argument("--control-path", default=os.environ.get("LSW_GPU_TEST_CONTROL_PATH", "/tmp/lsw-gpu-test-%C"))
    command.add_argument("--silent-after", type=float, default=30.0, metavar="SECONDS")
    command.add_argument("--runs", type=int, default=6, help="how many runs are kept")
    serve(parser.parse_args())


if __name__ == "__main__":
    main()
