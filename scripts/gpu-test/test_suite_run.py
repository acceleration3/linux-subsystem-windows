import fcntl
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import suite_run  # noqa: E402


def context(filter_text="", limit_hit=False):
    args = types.SimpleNamespace(filter=filter_text, suite="sanity", timeout=3600, bits="64")
    return types.SimpleNamespace(args=args, limit_hit=limit_hit, incidents=[], desktop=[])


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


class StatusFlags(unittest.TestCase):
    def lines(self, status_url, phase="main"):
        args = types.SimpleNamespace(bits="64", suite="sanity", dump=False, status_url=status_url, run_id="20260101-000000-1-local-all-x64")
        ctx = types.SimpleNamespace(args=args, guest_dir="C:\\run", phase=phase)
        return suite_run.segment_lines(ctx, suite_run.Spec("gpu-x64", "uuid:aa", "/nonexistent"), 2, "sanity.env.*", None, False, 2)

    def test_without_a_status_url_the_command_line_has_no_status_flag(self):
        lines, shown = self.lines("")
        self.assertNotIn("--status", shown)
        self.assertFalse(any("--status" in line for line in lines))

    def test_a_status_url_reaches_every_invocation_with_the_run_and_the_segment(self):
        lines, shown = self.lines("http://gateway:5190/report")
        self.assertIn('--status-url "http://gateway:5190/report"', shown)
        self.assertIn('--status-run "20260101-000000-1-local-all-x64"', shown)
        self.assertIn('--status-label "segment 2 main"', shown)
        self.assertTrue(any(line.startswith("lsw-suites64.exe") and shown in line for line in lines))

    def test_a_run_without_phases_labels_the_segment_only(self):
        self.assertIn('--status-label "segment 2"', self.lines("http://gateway:5190/report", phase=None)[1])


class StatusAnnouncement(unittest.TestCase):
    def context(self, status_url):
        scripts = []
        args = types.SimpleNamespace(bits="32", status_url=status_url, run_id="20260101-000000-1-local-all-xboth")
        machine = types.SimpleNamespace(sh=lambda script, timeout=300: scripts.append((script, timeout)) or (0, ""))
        return types.SimpleNamespace(args=args, machine=machine), scripts

    def specs(self):
        return [suite_run.Spec("gpu-a-x32", "uuid:00AA-00bb", "/nonexistent"), suite_run.Spec("gpu-b-x32", "uuid:cc", "/nonexistent")]

    def test_the_expected_parts_are_keyed_like_the_executable_keys_its_reports(self):
        ctx, _ = self.context("http://gateway:5190/report")
        self.assertEqual(suite_run.expected_parts(ctx.args, self.specs()),
                         [{"part": "00aa00bb-x32", "adapter": "gpu-a", "bits": 32}, {"part": "cc-x32", "adapter": "gpu-b", "bits": 32}])
        self.assertEqual(suite_run.expected_parts(ctx.args, [suite_run.Spec("x-x32", "", "/nonexistent")]), [])

    def test_the_run_and_its_parts_are_posted_to_the_status_server_on_the_vm_host(self):
        ctx, scripts = self.context("http://gateway:5190/report")
        suite_run.announce(ctx, self.specs()).join(5)
        script, timeout = scripts[0]
        self.assertIn("http://127.0.0.1:5190/expect", script)
        self.assertIn("curl -s -m 3", script)
        self.assertLessEqual(timeout, 10)
        body = json.loads(script.splitlines()[1])
        self.assertEqual((body["run"], [part["part"] for part in body["parts"]]), ("20260101-000000-1-local-all-xboth", ["00aa00bb-x32", "cc-x32"]))

    def test_nothing_is_posted_without_a_status_url_or_without_a_known_adapter(self):
        ctx, scripts = self.context("")
        self.assertIsNone(suite_run.announce(ctx, self.specs()))
        ctx, scripts_known = self.context("http://gateway:5190/report")
        self.assertIsNone(suite_run.announce(ctx, [suite_run.Spec("x-x32", "", "/nonexistent")]))
        self.assertEqual(scripts + scripts_known, [])


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


class RecoveryCredit(unittest.TestCase):
    def make(self):
        args = types.SimpleNamespace(run_id="r", smb_port=1, vm="local", host="h")
        return suite_run.Context(args)

    def test_a_deadline_without_recovery_ends_on_time(self):
        ctx = self.make()
        with mock.patch.object(suite_run.time, "monotonic", return_value=100.0):
            deadline = suite_run.Deadline(ctx, 50)
        with mock.patch.object(suite_run.time, "monotonic", return_value=149.0):
            self.assertFalse(deadline.passed())
        with mock.patch.object(suite_run.time, "monotonic", return_value=151.0):
            self.assertTrue(deadline.passed())

    def test_a_recovery_moves_the_deadline_by_the_time_it_took(self):
        ctx = self.make()
        with mock.patch.object(suite_run.time, "monotonic", return_value=100.0):
            deadline = suite_run.Deadline(ctx, 50)
        ctx.credit_recovery(300)
        with mock.patch.object(suite_run.time, "monotonic", return_value=400.0):
            self.assertFalse(deadline.passed())
        with mock.patch.object(suite_run.time, "monotonic", return_value=451.0):
            self.assertTrue(deadline.passed())

    def test_the_credit_has_a_hard_cap(self):
        ctx = self.make()
        ctx.credit_recovery(suite_run.RECOVERY_CREDIT_CAP * 5)
        ctx.credit_recovery(100)
        self.assertEqual(ctx.credit, suite_run.RECOVERY_CREDIT_CAP)

    def test_a_deadline_made_after_a_recovery_does_not_inherit_it(self):
        ctx = self.make()
        ctx.credit_recovery(300)
        with mock.patch.object(suite_run.time, "monotonic", return_value=100.0):
            deadline = suite_run.Deadline(ctx, 50)
        with mock.patch.object(suite_run.time, "monotonic", return_value=151.0):
            self.assertTrue(deadline.passed())

    def test_recover_credits_its_own_duration_even_when_it_fails(self):
        ctx = self.make()
        clock = iter([1000.0, 1240.0])

        def broken(reason, seen):
            raise RuntimeError("boom")

        ctx.recover_vm = broken
        with mock.patch.object(suite_run.time, "monotonic", side_effect=lambda: next(clock)):
            with self.assertRaises(RuntimeError):
                ctx.recover("dead", 0)
        self.assertEqual(ctx.credit, 240.0)


class FakeLink:
    def __init__(self, ctx):
        self.ctx = ctx

    def call(self, command, params=None, timeout=None):
        self.ctx.calls.append((command, params))
        if command == "list_windows":
            return {"windows": self.ctx.listings.pop(0) if len(self.ctx.listings) > 1 else self.ctx.listings[0]}
        return {"ok": True}

    def close(self):
        pass


def window(hwnd, path, title="", pid=10):
    return {"hwnd": hwnd, "pid": pid, "title": title, "exe_path": path}


class DesktopClear(unittest.TestCase):
    def make(self, *listings):
        ctx = types.SimpleNamespace(args=types.SimpleNamespace(host="h"), desktop=[], calls=[], listings=list(listings))
        ctx.link = lambda timeout=60.0: FakeLink(ctx)
        return ctx

    def test_suite_shell_and_agent_windows_are_never_recorded(self):
        found = suite_run.stray_windows([
            window(1, "C:\\av\\render-run\\x\\lsw-suites64.exe"), window(2, "C:\\Windows\\explorer.exe", "Program Manager"),
            window(3, "C:\\Windows\\System32\\cmd.exe"), window(4, "C:\\av\\lsw-guest-agent.exe"),
            window(5, "C:\\Windows\\SystemApps\\StartMenuExperienceHost.exe"), window(6, "")])
        self.assertEqual(found, [])

    def test_another_application_window_is_stray(self):
        found = suite_run.stray_windows([window(7, "C:\\Program Files\\Browser\\browser.exe", "Welcome", 77)])
        self.assertEqual([(w["process"], w["title"], w["hwnd"]) for w in found], [("browser.exe", "Welcome", 7)])

    def test_a_stray_window_is_recorded_and_left_alone(self):
        stray = window(7, "C:\\Browser\\browser.exe", "Welcome", 77)
        ctx = self.make([stray])
        suite_run.record_desktop(ctx, "isolated")
        self.assertEqual([c for c, _ in ctx.calls], ["list_windows"])
        self.assertEqual([(r["process"], r["title"], r["phase"]) for r in ctx.desktop], [("browser.exe", "Welcome", "isolated")])

    def test_a_window_is_recorded_once_per_phase_while_the_desktop_settles(self):
        late = window(7, "C:\\Browser\\browser.exe", "Welcome", 77)
        ctx = self.make([window(1, "C:\\Windows\\explorer.exe")], [late], [late], [late])
        clock = iter([0.0, 3.0, 6.0, 9.0, 31.0])
        with mock.patch.object(suite_run.time, "sleep"), mock.patch.object(
                suite_run.time, "monotonic", side_effect=lambda: next(clock)):
            suite_run.settle_desktop(ctx, "isolated", window=30.0, interval=3.0)
        self.assertEqual([r["process"] for r in ctx.desktop], ["browser.exe"])
        self.assertNotIn("close_window", [c for c, _ in ctx.calls])
        self.assertNotIn("run_script", [c for c, _ in ctx.calls])

    def test_a_clean_desktop_records_nothing(self):
        ctx = self.make([window(1, "C:\\Windows\\explorer.exe")])
        suite_run.record_desktop(ctx, "isolated")
        self.assertEqual(ctx.desktop, [])
        self.assertEqual([c for c, _ in ctx.calls], ["list_windows"])

    def test_the_incident_is_written_to_the_result_directory(self):
        ctx = types.SimpleNamespace(args=types.SimpleNamespace(suite="sanity", timeout=1, bits="64", filter=""), limit_hit=False,
                                    incidents=[], desktop=[{"time": "t", "phase": "isolated", "process": "browser.exe",
                                                            "title": "Welcome"}])
        with tempfile.TemporaryDirectory() as directory:
            spec = suite_run.Spec("x", "", directory)
            ctx.share = types.SimpleNamespace(get=mock.Mock(side_effect=suite_run.lsw_link.LinkError("none")))
            ctx.guest_dir = "C:\\g"
            with mock.patch.object(suite_run, "pull_archive", return_value=True):
                suite_run.finish(ctx, spec)
            text = (pathlib.Path(directory) / "desktop-incidents.txt").read_text()
            result = json.loads((pathlib.Path(directory) / "result.json").read_text())
        self.assertIn("browser.exe", text)
        self.assertIn("Welcome", text)
        self.assertEqual(result["desktop_incidents"][0]["process"], "browser.exe")


if __name__ == "__main__":
    unittest.main()
