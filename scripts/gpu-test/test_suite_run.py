import pathlib
import sys
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


if __name__ == "__main__":
    unittest.main()
