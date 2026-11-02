import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gpu_matrix  # noqa: E402

GPU = "AMD Ryzen 9 9950X3D 16-Core Processor (RADV RAPHAEL_MENDOCINO)"


def sample(name, host="ok", vm="ok", picture="identical", tone="good"):
    return {"name": name, "host_status": host, "vm_status": vm, "picture": picture, "tone": tone}


class JudgeSummary(unittest.TestCase):
    def judge(self, samples, api="d3d11", gpu=GPU):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "summary.json"
            path.write_text(json.dumps({"api": "D3D11", "samples": samples}))
            return gpu_matrix.judge_summary(path, api, gpu)

    def test_matching_pictures_pass(self):
        failures, notes, count = self.judge([sample("triangle"), sample("gears", picture="matches")])
        self.assertEqual((failures, notes, count), ([], [], 2))

    def test_host_that_never_ran_fails_every_sample(self):
        samples = [sample(n, host=None, picture="missing", tone="bad") for n in ("triangle", "texture")]
        failures, notes, _ = self.judge(samples)
        self.assertEqual([f["case"] for f in failures], ["triangle", "texture"])
        self.assertIn("host: did not run", failures[0]["detail"])
        self.assertEqual(notes, [])

    def test_host_init_failure_fails_even_when_vm_ran(self):
        failures, _, _ = self.judge([sample("tessellation", host="init-failed", picture="missing", tone="bad")])
        self.assertEqual(failures, [{"case": "tessellation", "detail": "host: init-failed; VM: ok"}])

    def test_both_halves_failing_is_a_failure_not_a_note(self):
        failures, notes, _ = self.judge([sample("multithreading", host="crashed (-11)", vm="crashed (?)",
                                                picture="missing", tone="bad")])
        self.assertEqual(len(failures), 1)
        self.assertEqual(notes, [])

    def test_vm_failure_with_host_ok_fails(self):
        failures, _, _ = self.judge([sample("bloom", vm=None, picture="missing", tone="bad")])
        self.assertEqual(failures, [{"case": "bloom", "detail": "VM: did not run; host: ok"}])

    def test_differing_picture_fails_and_close_one_is_noted(self):
        failures, notes, _ = self.judge([sample("a", picture="differs", tone="bad"),
                                         sample("b", picture="close", tone="warn")])
        self.assertEqual([f["case"] for f in failures], ["a"])
        self.assertEqual(notes, [{"case": "b", "detail": "picture close"}])

    def test_excepted_sample_is_noted_with_its_reason(self):
        exceptions = (("d3d11", "texture", "Ryzen", "the 610M lacks X natively"),)
        with mock.patch.object(gpu_matrix, "HOST_EXCEPTIONS", exceptions):
            failures, notes, _ = self.judge([sample("texture", host=None, picture="missing", tone="bad")])
        self.assertEqual(failures, [])
        self.assertEqual(len(notes), 1)
        self.assertIn("excepted: the 610M lacks X natively", notes[0]["detail"])

    def test_exception_holds_only_for_its_suite_sample_and_gpu(self):
        exceptions = (("d3d11", "texture", "Ryzen", "why"),)
        missing = dict(host=None, picture="missing", tone="bad")
        with mock.patch.object(gpu_matrix, "HOST_EXCEPTIONS", exceptions):
            self.assertEqual(len(self.judge([sample("texture", **missing)], api="d3d12")[0]), 1)
            self.assertEqual(len(self.judge([sample("gears", **missing)])[0]), 1)
            self.assertEqual(len(self.judge([sample("texture", **missing)], gpu="NVIDIA GeForce RTX 3070 Ti")[0]), 1)

    def test_no_exception_is_registered(self):
        self.assertEqual(gpu_matrix.HOST_EXCEPTIONS, ())


class RunReport(unittest.TestCase):
    def test_suite_with_missing_host_pictures_reports_fail(self):
        class Gpu:
            name = GPU
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            (out / "summary.json").write_text(json.dumps({"samples": [
                sample("triangle", host=None, picture="missing", tone="bad")]}))
            done = mock.Mock(returncode=0)
            with mock.patch.object(gpu_matrix.subprocess, "run", return_value=done), \
                 mock.patch.object(gpu_matrix, "report_command", return_value=["true"]), \
                 mock.patch.object(gpu_matrix, "host_environment", return_value={}), \
                 mock.patch.object(gpu_matrix, "say"):
                result = gpu_matrix.run_report("d3d11", out, out, None, Gpu(), "user")
        self.assertEqual(result["status"], "fail")
        self.assertEqual(result["failures"][0]["case"], "triangle")


if __name__ == "__main__":
    unittest.main()
