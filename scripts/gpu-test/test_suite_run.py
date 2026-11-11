import fcntl
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

    def test_every_listed_row_is_expected_when_one_phase_runs_all(self):
        self.assertEqual(suite_run.expected_rows([["all", "parallel"]], ["render.i"], ["render.a", "render.b"]),
                         ["render.i", "render.a", "render.b"])

    def test_split_phases_expect_their_own_rows(self):
        phases = [["main", "parallel"], ["isolated", "serial"]]
        self.assertEqual(suite_run.expected_rows(phases, ["sanity.i"], ["sanity.a"]), ["sanity.a", "sanity.i"])
        self.assertEqual(suite_run.expected_rows([["isolated", "serial"]], ["sanity.i"], ["sanity.a"]), ["sanity.i"])

    def test_reason_names_the_recovery_limit(self):
        spec = spec_with()
        suite_run.report_not_run(context(limit_hit=True), spec, ["sanity.b"])
        self.assertIn("recovery limit", spec.finished["sanity.b"]["reason"])


class VmLock(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.lock = pathlib.Path(self.temp.name) / "vm.lock"
        self.old = os.environ.get("LSW_RENDER_HEAVY_LOCK")
        os.environ["LSW_RENDER_HEAVY_LOCK"] = str(self.lock)
        self.machine = suite_run.Machine("local", "test")

    def tearDown(self):
        if self.old is None:
            os.environ.pop("LSW_RENDER_HEAVY_LOCK", None)
        else:
            os.environ["LSW_RENDER_HEAVY_LOCK"] = self.old
        self.temp.cleanup()

    def test_the_lock_is_seen_only_while_someone_holds_it(self):
        self.assertFalse(self.machine.vm_lock_held())
        with open(self.lock, "w") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX)
            self.assertTrue(self.machine.vm_lock_held())
        self.assertFalse(self.machine.vm_lock_held())

    def test_prune_touches_no_guest_folder_without_the_lock(self):
        class Share:
            calls = []

            share_path = staticmethod(suite_run.lsw_link.GuestShare.share_path)

            def smbclient(self, *args):
                self.calls.append(args)
                return 0, ""

            def remove(self, path):
                raise OSError(path)

        ctx = types.SimpleNamespace(machine=self.machine, share=Share(), args=types.SimpleNamespace(host="test", run_id="r"))
        suite_run.prune(ctx)
        self.assertEqual(Share.calls, [])
        with open(self.lock, "w") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX)
            suite_run.prune(ctx)
        self.assertEqual(len(Share.calls), 1)


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

    def test_a_process_a_killed_tool_left_stopped_runs_again(self):
        self.tool("eu-stack", f"kill -STOP {self.victim.pid}; sleep 100")
        self.tool("gdb", "sleep 100")
        self.collect(budget=2)
        state = pathlib.Path(f"/proc/{self.victim.pid}/stat").read_text().rsplit(")", 1)[1].split()[0]
        self.assertNotEqual(state, "T")

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


class LogTail(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = pathlib.Path(self.temp.name)
        self.logs = self.home / ".cache" / "libvirt" / "qemu" / "log"
        self.logs.mkdir(parents=True)
        self.old_home = os.environ.get("HOME")
        os.environ["HOME"] = str(self.home)
        self.machine = suite_run.Machine("local", "test")

    def tearDown(self):
        if self.old_home is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = self.old_home
        self.temp.cleanup()

    def write(self, name, text):
        (self.logs / name).write_bytes(text)

    def test_rotated_files_come_before_the_current_one_oldest_first(self):
        self.write("lsw.log", b"current\n")
        self.write("lsw.log.0", b"newest rotated\n")
        self.write("lsw.log.1", b"older\n")
        self.write("lsw.log.10", b"oldest\n")
        self.assertEqual(self.machine.log_tail(), "oldest\nolder\nnewest rotated\ncurrent\n")

    def test_the_tail_is_bounded_and_keeps_the_end(self):
        self.write("lsw.log.0", b"a" * 100)
        self.write("lsw.log", b"b" * 100)
        self.assertEqual(self.machine.log_tail(150), "a" * 50 + "b" * 100)

    def test_bytes_that_are_not_text_do_not_break_the_save(self):
        self.write("lsw.log", b"before \xff\xfe after\n")
        target = self.home / "out" / "qemu-log-s1.txt"
        self.machine.save_log_tail(target)
        self.assertIn("after", target.read_text())

    def test_a_missing_log_gives_an_empty_file(self):
        target = self.home / "out" / "qemu-log-s1.txt"
        self.machine.save_log_tail(target)
        self.assertEqual(target.read_text(), "")

    def test_evidence_before_a_restart_keeps_the_log_tail(self):
        self.write("lsw.log", b"crash trace\n")
        original = self.machine.sh
        self.machine.sh = lambda script, timeout=300: (0, "no core") if "coredumpctl" in script else original(script, timeout)
        out = self.home / "evidence"
        self.machine.evidence(out, set())
        self.assertEqual((out / "qemu-log-tail.txt").read_text(), "crash trace\n")


if __name__ == "__main__":
    unittest.main()
