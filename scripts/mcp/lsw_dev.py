#!/usr/bin/env python3
import json
import os
import pathlib
import subprocess
import sys
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "external" / "accel-virt" / "tools" / "mcp"))
from mcp_server import BuildLimits, Jobs, JobTools, Server, ToolError, state_directory, tool  # noqa: E402

SERVER = {"name": "lsw-dev", "version": "1"}
SCRIPTS = ROOT / "scripts"
BUILD = ROOT / "build"
HOSTS_FILE = SCRIPTS / "gpu-test" / "hosts"
MANAGER_URL = os.environ.get("LSW_MCP_URL", "http://127.0.0.1:5105/")
MANAGER_TIMEOUT = 5
QUICK_TIMEOUT = 120
DEFAULT_HOST_TESTS = "lsw_host_|linkproto|agent_"
REPOSITORIES = {"lsw": ROOT, "accel-virt": ROOT / "external" / "accel-virt", "remotewin": ROOT / "external" / "remotewin"}
BUILD_TARGETS = {
    "manager": ["cmake", "--build", str(BUILD), "--target", "lsw-manager"],
    "remotewin": ["cmake", "--build", str(BUILD), "--target", "remotewin"],
    "accel-virt": ["cmake", "--build", str(BUILD), "--target", "accel_virt"],
    "host-tests": ["cmake", "--build", str(BUILD)],
    "agent": [str(SCRIPTS / "build-agent.sh")],
    "suites": [str(SCRIPTS / "build-suites.sh")],
    "render-ref": [str(SCRIPTS / "build-render-ref.sh")],
    "all": [str(SCRIPTS / "build.sh")],
    "all-without-gpu": [str(SCRIPTS / "build.sh"), "--no-gpu"],
}
REMOTE_ACTIONS = {"master": True, "gpus": True, "stack-status": True, "stack-restore": False}
UPGRADE_OPTIONS = {"make-baseline", "pause-vm", "restore-user", "keep", "x86"}


class ManagerClient:
    def __init__(self, url=MANAGER_URL):
        self.url = url

    def call(self, name, arguments=None, timeout=MANAGER_TIMEOUT):
        body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                           "params": {"name": name, "arguments": arguments or {}}}).encode()
        request = urllib.request.Request(self.url, body, {"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                answer = json.loads(response.read())
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            raise ToolError(f"the manager's MCP server at {self.url} did not answer ({error}); is lsw-manager running?")
        if "error" in answer:
            raise ToolError(f"manager: {answer['error'].get('message')}")
        result = answer.get("result", {})
        text = "".join(part.get("text", "") for part in result.get("content", []))
        if result.get("isError"):
            raise ToolError(f"manager {name}: {text}")
        return text


class Memory:
    def __init__(self, directory):
        self.path = pathlib.Path(directory) / "memory.json"

    def get(self, key):
        try:
            return json.loads(self.path.read_text()).get(key)
        except (OSError, ValueError):
            return None

    def put(self, key, value):
        data = {}
        try:
            data = json.loads(self.path.read_text())
        except (OSError, ValueError):
            pass
        data[key] = value
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(data))


class LswDev(JobTools):
    def __init__(self):
        directory = state_directory("lsw-dev")
        super().__init__(Jobs(directory / "jobs"))
        self.limits = BuildLimits()
        self.manager = ManagerClient()
        self.memory = Memory(directory)

    def tools(self):
        stack = {"type": "string", "description": "a stack directory from stack_build; default: the last one used"}
        host = {"type": "string", "description": "a host name from the hosts file (local, test, accel)"}
        return [
            tool("status", "One look at the development state: branch and changes of LSW, accel-virt and remotewin, "
                 "whether the VM and the manager run, whether a build holds the build lock, the verify queue and "
                 "the running jobs."),
            tool("build", "Builds one part of LSW as a background job, at four CPUs under the build lock. manager, "
                 "remotewin, accel-virt and host-tests build in build/; agent cross-builds the guest agent and "
                 "remotewin-guest; suites builds lsw-suites64/32.exe; render-ref the native references; all runs "
                 "scripts/build.sh.",
                 {"target": {"type": "string", "enum": sorted(BUILD_TARGETS)},
                  "bits": {"type": "string", "enum": ["64", "32", "both"], "description": "suites only"}},
                 ["target"]),
            tool("test", "Runs LSW's tests as a background job: host runs the ctest tests of build/ (the CI set "
                 "unless filter is given), python runs the unit tests of scripts/.",
                 {"kind": {"type": "string", "enum": ["host", "python"]},
                  "filter": {"type": "string", "description": "ctest -R regular expression (host only)"}}, ["kind"]),
            tool("deploy_agent", "Builds the guest agent and remotewin-guest.exe and hot-swaps them into the running "
                 "VM through the manager. Needs lsw-manager running with Windows up.",
                 {"build": {"type": "boolean", "description": "default true; false deploys the existing build"}}),
            tool("vm", "The local lsw VM: status, up (start and wait for the agent, a job), down (shut it down), "
                 "done (mark it as just used for the idle watcher). Keep it down when nothing needs it.",
                 {"action": {"type": "string", "enum": ["status", "up", "down", "done"]}}, ["action"]),
            tool("stack_build", "Builds a whole driver stack from a clean, committed accel-virt worktree into a stack "
                 "directory (executor, guest drivers x64 and x32, Direct3D runtime, QEMU modules, manifest.json). "
                 "It takes the build lock itself.",
                 {"driver_worktree": {"type": "string", "description": "default external/accel-virt"},
                  "out": {"type": "string", "description": "the stack directory to write"},
                  "dry_run": {"type": "boolean"}, "allow_dirty": {"type": "boolean"}}, ["out"]),
            tool("verify", "Runs guest suite rows on a host through scripts/verify.sh, the only supported path: it "
                 "waits its turn in the VM queue, deploys exactly the stack, checks what QEMU maps against the "
                 "manifest and writes <stack>/results. Needs a filter or smoke; a whole suite needs full=true and "
                 "the user's go-ahead. Check gpu_kernel_events afterwards.",
                 {"host": host, "stack": stack,
                  "filter": {"type": "string", "description": "comma-separated check id globs"},
                  "smoke": {"type": "boolean", "description": "the fixed smoke gate of scripts/smoke-selection.txt"},
                  "full": {"type": "boolean", "description": "run the whole suite (ask the user first)"},
                  "suite": {"type": "string", "enum": ["render", "sanity"]},
                  "adapters": {"type": "string", "enum": ["all", "first"]},
                  "gpus": {"type": "string"}, "bits": {"type": "string", "enum": ["64", "32", "both"]},
                  "jobs": {"type": "integer"}, "max_hold": {"type": "integer", "description": "seconds, default 3600"},
                  "redeploy": {"type": "boolean"}, "dry_run": {"type": "boolean"}}, ["host"]),
            tool("verify_queue", "Who holds or waits for each host's VM in the verify queue."),
            tool("results", "The newest result directories of a stack, with each smoke summary.",
                 {"stack": stack, "limit": {"type": "integer"}}),
            tool("report", "Writes the side-by-side HTML report of a result directory (a job).",
                 {"results": {"type": "string"}, "baseline": {"type": "string"}}, ["results"]),
            tool("hosts", "The GPU test hosts of the hosts file, with whether each one's ssh master connection is "
                 "open. The user opens masters; the hint says how."),
            tool("remote", "One remote host action through scripts/gpu-test/gpu-test.sh: master (is the ssh master "
                 "open), gpus (its GPUs and which drives a display), stack-status (the stack and disk cache its VM "
                 "runs), stack-restore (put its original stack back, a job).",
                 {"action": {"type": "string", "enum": sorted(REMOTE_ACTIONS)}, "host": host}, ["action", "host"]),
            tool("package", "Builds release packages in their distroboxes through scripts/dist.sh (a long job).",
                 {"targets": {"type": "array", "items": {"type": "string", "enum": ["arch", "deb", "rpm"]}},
                  "version": {"type": "string"}, "incremental": {"type": "boolean"}}),
            tool("upgrade_test", "Proves that master upgrades to the current branch on an overlay VM "
                 "(scripts/test-upgrade.sh). It stops and restarts the real VM and manager and runs for a long "
                 "time: ask the user first.",
                 {"options": {"type": "array", "items": {"type": "string", "enum": sorted(UPGRADE_OPTIONS)}}}),
            tool("guest_update_test", "In-VM scenario tests of the guest update script (scripts/test-guest-update.sh). "
                 "Needs the manager and Windows up.",
                 {"only": {"type": "string", "description": "scenario letters"}, "keep": {"type": "boolean"}}),
            tool("tidy", "clang-tidy over the given paths of build/compile_commands.json (a job).",
                 {"paths": {"type": "array", "items": {"type": "string"}}}),
            *self.job_tools(),
        ]

    def run(self, command, timeout=QUICK_TIMEOUT, cwd=ROOT):
        result = subprocess.run(command, cwd=str(cwd), capture_output=True, text=True, timeout=timeout)
        return result.returncode, (result.stdout + result.stderr).strip()

    def repository_state(self, path):
        if not (path / ".git").exists():
            return {"present": False}
        _, branch = self.run(["git", "-C", str(path), "rev-parse", "--abbrev-ref", "HEAD"])
        _, head = self.run(["git", "-C", str(path), "log", "-1", "--format=%h %s"])
        _, changes = self.run(["git", "-C", str(path), "status", "--porcelain", "--ignore-submodules=dirty"])
        _, counts = self.run(["git", "-C", str(path), "rev-list", "--left-right", "--count", "@{upstream}...HEAD"])
        state = {"branch": branch, "head": head[:100], "changed_files": len(changes.splitlines())}
        if counts and counts.split()[0].isdigit():
            behind, ahead = counts.split()
            state.update(behind=int(behind), ahead=int(ahead))
        return state

    def build_lock_held(self):
        code, _ = self.run(["flock", "-n", str(self.limits.lock), "true"], timeout=5)
        return code != 0

    def status(self):
        _, vm = self.run([str(SCRIPTS / "render-vm.sh"), "status"], timeout=30)
        try:
            manager = json.loads(self.manager.call("system_info", timeout=3))
            manager = {"running": True, "agent_version": manager.get("agent_version")}
        except (ToolError, ValueError) as error:
            manager = {"running": False, "detail": str(error)[:160]}
        queue = self.verify_queue()
        return {"repositories": {name: self.repository_state(path) for name, path in REPOSITORIES.items()},
                "vm": vm, "manager": manager, "build_lock_held": self.build_lock_held(),
                "verify_queue": queue or "empty",
                "running_jobs": [job for job in self.jobs.list() if job["state"] == "running"],
                "last_stack": self.memory.get("stack")}

    def start_build(self, name, command, env=None):
        environment = dict(self.limits.environment())
        environment.update(env or {})
        return self.jobs.start(name, self.limits.wrap(command), ROOT, environment, exclusive="build")

    def build(self, target, bits=None):
        if target in ("manager", "remotewin", "accel-virt", "host-tests") and not (BUILD / "CMakeCache.txt").exists():
            raise ToolError("build/ is not configured; build target all once first")
        env = {"LSW_SUITES_BITS": bits} if bits and target == "suites" else None
        return self.start_build(f"build-{target}", BUILD_TARGETS[target], env)

    def test(self, kind, filter=None):
        if kind == "host":
            command = ["ctest", "--test-dir", str(BUILD), "-R", filter or DEFAULT_HOST_TESTS, "--output-on-failure",
                       "-j", self.limits.jobs]
            if not filter:
                command += ["-E", "^lsw_host_agent_files_test$"]
            return self.start_build("test-host", command)
        files = sorted(str(path.relative_to(ROOT)) for path in SCRIPTS.rglob("test_*.py"))
        script = 'failed=(); for test in "$@"; do echo "== $test"; python3 "$test" || failed+=("$test"); done; ' \
                 '(( ${#failed[@]} == 0 )) || { echo "failed: ${failed[*]}"; exit 1; }'
        return self.jobs.start("test-python", ["bash", "-c", script, "python-tests", *files], ROOT)

    def deploy_agent(self, build=True):
        self.manager.call("system_info")
        command = [str(SCRIPTS / "redeploy-agent.sh")] + ([] if build else ["--no-build"])
        if build:
            return self.start_build("deploy-agent", command)
        return self.jobs.start("deploy-agent", command, ROOT, exclusive="vm:local")

    def vm(self, action):
        if action == "up":
            return self.jobs.start("vm-up", [str(SCRIPTS / "render-vm.sh"), "up"], ROOT, exclusive="vm-power")
        code, output = self.run([str(SCRIPTS / "render-vm.sh"), action], timeout=300)
        if code != 0:
            raise ToolError(output)
        return output

    def stack_directory(self, stack):
        stack = stack or self.memory.get("stack")
        if not stack:
            raise ToolError("no stack given and none used before")
        path = pathlib.Path(stack).expanduser().resolve()
        self.memory.put("stack", str(path))
        return path

    def stack_build(self, out, driver_worktree=None, dry_run=False, allow_dirty=False):
        worktree = pathlib.Path(driver_worktree or ROOT / "external" / "accel-virt").expanduser().resolve()
        path = self.stack_directory(out)
        command = [str(SCRIPTS / "stack-build.sh")] + (["--dry-run"] if dry_run else []) + [str(worktree), str(path)]
        env = {"LSW_STACK_ALLOW_DIRTY": "1"} if allow_dirty else None
        return self.jobs.start("stack-build", command, ROOT, env, exclusive="build")

    def verify(self, host, stack=None, filter=None, smoke=False, full=False, suite=None, adapters=None, gpus=None,
               bits=None, jobs=None, max_hold=None, redeploy=False, dry_run=False):
        if not (filter or smoke or full):
            raise ToolError("give a filter or smoke=true; a whole suite needs full=true after the user agreed")
        if filter and smoke:
            raise ToolError("smoke replaces the filter: give one of them")
        path = self.stack_directory(stack)
        command = [str(SCRIPTS / "verify.sh"), "--host", host, "--stack", str(path)]
        if smoke:
            command.append("--smoke")
        elif filter:
            command += ["--filter", filter]
        for flag, value in (("--suite", suite), ("--adapters", adapters), ("--gpus", gpus), ("--bits", bits),
                            ("--jobs", jobs), ("--max-hold", max_hold)):
            if value is not None:
                command += [flag, str(value)]
        command += (["--redeploy"] if redeploy else []) + (["--dry-run"] if dry_run else [])
        return self.jobs.start(f"verify-{host}", command, ROOT, exclusive=None if dry_run else f"vm:{host}")

    def verify_queue(self):
        queues = {}
        for entry in self.host_entries():
            _, output = self.run(["python3", "-B", str(SCRIPTS / "vm_queue.py"), "status", "--host", entry["name"]],
                                 timeout=10)
            queues[entry["name"]] = output or "free"
        return queues

    def results(self, stack=None, limit=8):
        folder = self.stack_directory(stack) / "results"
        if not folder.is_dir():
            return {"stack": str(folder.parent), "results": []}
        entries = sorted((entry for entry in folder.iterdir() if entry.is_dir()), reverse=True)[:limit]
        listing = []
        for entry in entries:
            summary = entry / "smoke-summary.txt"
            listing.append({"path": str(entry),
                            "smoke_summary": summary.read_text()[-1500:] if summary.is_file() else None})
        return {"stack": str(folder.parent), "results": listing}

    def report(self, results, baseline=None):
        tree = pathlib.Path(results).expanduser().resolve()
        command = ["python3", str(SCRIPTS / "render-report.py"), "--tree", str(tree), "--out", str(tree / "report.html")]
        if baseline:
            command += ["--baseline-tree", str(pathlib.Path(baseline).expanduser().resolve())]
        return self.jobs.start("report", command, ROOT)

    def host_entries(self):
        if not HOSTS_FILE.is_file():
            raise ToolError(f"{HOSTS_FILE.relative_to(ROOT)} does not exist; copy hosts.example and fill it in")
        entries = []
        for line in HOSTS_FILE.read_text().splitlines():
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            entries.append({"name": parts[0], "target": parts[1] if len(parts) > 1 else parts[0],
                            "settings": dict(part.split("=", 1) for part in parts[2:] if "=" in part)})
        return entries

    def hosts(self):
        entries = self.host_entries()
        for entry in entries:
            if entry["name"] == "local":
                entry["master"] = "not needed"
                continue
            code, output = self.run([str(SCRIPTS / "gpu-test" / "gpu-test.sh"), "master", entry["name"]], timeout=20)
            hint = output.splitlines()[-1].strip().lstrip("! ").strip() if output else ""
            entry["master"] = "open" if code == 0 else f"closed; the user opens it with: {hint}" if hint else "closed"
        return entries

    def remote(self, action, host):
        if host not in {entry["name"] for entry in self.host_entries()}:
            raise ToolError(f"{host} is not in the hosts file")
        command = [str(SCRIPTS / "gpu-test" / "gpu-test.sh"), action, host]
        if not REMOTE_ACTIONS[action]:
            return self.jobs.start(f"{action}-{host}", command, ROOT, exclusive=f"vm:{host}")
        code, output = self.run(command, timeout=QUICK_TIMEOUT)
        if code != 0:
            raise ToolError(output or f"gpu-test.sh {action} {host} exited {code}")
        return output

    def package(self, targets=None, version=None, incremental=False):
        command = [str(SCRIPTS / "dist.sh")] + (["--version", version] if version else [])
        command += (["--incremental"] if incremental else []) + list(targets or [])
        return self.start_build("package", command)

    def upgrade_test(self, options=None):
        command = [str(SCRIPTS / "test-upgrade.sh")] + [f"--{option}" for option in options or []]
        return self.jobs.start("upgrade-test", command, ROOT, exclusive="vm:local")

    def guest_update_test(self, only=None, keep=False):
        self.manager.call("system_info")
        command = [str(SCRIPTS / "test-guest-update.sh")] + (["--only", only] if only else [])
        command += ["--keep"] if keep else []
        return self.jobs.start("guest-update-test", command, ROOT, exclusive="vm:local")

    def tidy(self, paths=None):
        if not (BUILD / "compile_commands.json").exists():
            raise ToolError("build/compile_commands.json is missing; build the manager first")
        return self.start_build("tidy", [str(SCRIPTS / "tidy.sh"), *(paths or [])])


def main():
    Server(LswDev(), SERVER).serve()


if __name__ == "__main__":
    main()
