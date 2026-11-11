import os
import pathlib
import subprocess
import sys
import tempfile
import time
import types
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import suite_run  # noqa: E402


def context(filter_text="", limit_hit=False):
    args = types.SimpleNamespace(filter=filter_text, suite="sanity", timeout=3600, bits="64")
    return types.SimpleNamespace(args=args, limit_hit=limit_hit, incidents=[])


def spec_with(*finished):
    spec = suite_run.Spec("x", "", "/nonexistent")
    for check_id in finished:
        spec.finished[check_id] = {"id": check_id, "status": "ok"}
        spec.order.append(check_id)
    return spec


class NotRun(unittest.TestCase):
    def test_listed_rows_without_a_result_are_reported_as_not_run(self):
        spec = spec_with("sanity.a")
        suite_run.report_not_run(context(), spec, ["sanity.a", "sanity.b", "sanity.c"])
        self.assertEqual(spec.order, ["sanity.a", "sanity.b", "sanity.c"])
        self.assertEqual(spec.finished["sanity.a"]["status"], "ok")
        self.assertEqual(spec.finished["sanity.b"]["status"], "not_run")

    def test_not_run_rows_count_as_failures(self):
        spec = spec_with("sanity.a")
        suite_run.report_not_run(context(), spec, ["sanity.b"])
        summary = suite_run.merged(context(), spec)["summary"]
        self.assertEqual((summary["passed"], summary["failed"]), (1, 1))

    def test_rows_outside_the_filter_are_not_reported(self):
        spec = spec_with()
        suite_run.report_not_run(context("sanity.p*,-sanity.pz*"), spec, ["sanity.p1", "sanity.q1", "sanity.pz1"])
        self.assertEqual(spec.order, ["sanity.p1"])

    def test_reason_names_the_recovery_limit(self):
        spec = spec_with()
        suite_run.report_not_run(context(limit_hit=True), spec, ["sanity.b"])
        self.assertIn("recovery limit", spec.finished["sanity.b"]["reason"])


class StackEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.temp.name)
        self.bin = root / "bin"
        self.bin.mkdir()
        self.run_dir = root / "run" / "libvirt" / "qemu" / "run"
        self.run_dir.mkdir(parents=True)
        self.out = root / "evidence"
        self.victim = subprocess.Popen(["sleep", "60"])
        (self.run_dir / "lsw.pid").write_text(str(self.victim.pid))
        self.tool("virsh", 'echo "${DOMSTATE:-running}"')
        self.old_path = os.environ.get("PATH", "")
        self.old_runtime = os.environ.get("XDG_RUNTIME_DIR")
        os.environ["PATH"] = f"{self.bin}:{self.old_path}"
        os.environ["XDG_RUNTIME_DIR"] = str(root / "run")
        self.machine = suite_run.Machine("local", "test")

    def tearDown(self):
        self.victim.kill()
        self.victim.wait()
        os.environ["PATH"] = self.old_path
        if self.old_runtime is None:
            os.environ.pop("XDG_RUNTIME_DIR", None)
        else:
            os.environ["XDG_RUNTIME_DIR"] = self.old_runtime
        os.environ.pop("DOMSTATE", None)
        self.temp.cleanup()

    def tool(self, name, body):
        path = self.bin / name
        path.write_text("#!/bin/sh\n" + body + "\n")
        path.chmod(0o755)

    def hide(self, name):
        self.tool(name, "exit 127")
        (self.bin / name).unlink()

    def collect(self, budget=suite_run.STACK_BUDGET):
        self.out.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        code, text = self.machine.sh(self.machine.stack_script(budget), budget + 15)
        return text, time.monotonic() - started

    def test_a_running_domain_yields_thread_files_and_eu_stack_output(self):
        self.tool("eu-stack", 'echo "TID 1: frame main"')
        text, _ = self.collect()
        self.assertIn(f"pid {self.victim.pid}", text)
        self.assertIn("comm: sleep", text)
        self.assertIn("wchan:", text)
        self.assertIn("stat: ", text)
        self.assertIn("tool: eu-stack", text)
        self.assertIn("TID 1: frame main", text)
        self.assertNotIn("tool: gdb", text)

    def test_gdb_is_the_fallback_when_eu_stack_is_missing_or_fails(self):
        self.tool("gdb", 'echo "$@"; echo "Thread 1 bt"')
        self.tool("eu-stack", "exit 1")
        text, _ = self.collect()
        self.assertIn("tool: gdb", text)
        self.assertIn("thread apply all bt", text)
        self.assertIn("Thread 1 bt", text)
        self.hide("eu-stack")
        text, _ = self.collect()
        self.assertIn("tool: gdb", text)

    def test_a_wedged_tool_is_killed_within_the_budget(self):
        self.tool("eu-stack", "sleep 100")
        self.tool("gdb", "sleep 100")
        text, seconds = self.collect(budget=4)
        self.assertLess(seconds, 12)
        self.assertIn("comm: sleep", text)
        self.assertIn("no stack tool produced output", text)

    def test_a_domain_that_is_not_running_collects_nothing(self):
        os.environ["DOMSTATE"] = "shut off"
        self.assertFalse(self.machine.stacks(self.out))
        self.assertFalse((self.out / "qemu-stacks.txt").exists())

    def test_evidence_saves_the_stacks_before_the_restart_evidence(self):
        self.tool("eu-stack", 'echo "TID 7: frame poll"')
        scripts = []
        original = self.machine.sh

        def recording(script, timeout=300):
            scripts.append(script)
            if "coredumpctl" in script:
                return 0, "no core"
            if "tail -n 120" in script:
                return 0, "log"
            return original(script, timeout)

        self.machine.sh = recording
        self.out.mkdir(parents=True)
        self.machine.evidence(self.out, set())
        self.assertIn("TID 7: frame poll", (self.out / "qemu-stacks.txt").read_text())
        self.assertIn("eu-stack", scripts[0])
        self.assertIn("coredumpctl", scripts[-1])


if __name__ == "__main__":
    unittest.main()
