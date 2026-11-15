import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import smoke_baseline as sb  # noqa: E402
import smoke_verdict as sv  # noqa: E402


def row(check_id, status="ok", ms=100):
    return {"id": check_id, "status": status, "ms": ms}


def result(rows, **extra):
    data = {"complete": True, "crashes": [], "host_crashes": [], "incidents": [], "checks": rows}
    data.update(extra)
    return data


BASELINE = {"rows": {"a": 100.0, "b": 200.0, "c": 5000.0}}


class BaselineTests(unittest.TestCase):
    def test_update_keeps_ok_rows_of_the_selection_with_their_median(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = pathlib.Path(tmp, "1.json")
            second = pathlib.Path(tmp, "2.json")
            first.write_text(json.dumps(result([row("render.x", ms=100), row("render.y", "fail", 5), row("render.z", ms=1)])))
            second.write_text(json.dumps(result([row("render.x", ms=300), row("render.y", ms=7)])))
            baseline = sb.build([first, second], ["render.x", "render.y"])
        self.assertEqual(baseline["rows"], {"render.x": 200.0, "render.y": 7.0})

    def test_row_limit_is_the_larger_of_five_times_and_three_seconds_more(self):
        self.assertEqual(sb.row_limit(100), 3100)
        self.assertEqual(sb.row_limit(2000), 10000)

    def test_slow_row_is_named_and_a_row_within_the_limit_is_not(self):
        report = sb.check_budget([("a", 3000), ("b", 200), ("c", 5000)], BASELINE)
        self.assertEqual([r["id"] for r in report["slow_rows"]], [])
        report = sb.check_budget([("a", 3101), ("b", 200), ("c", 5000)], BASELINE)
        self.assertEqual([r["id"] for r in report["slow_rows"]], ["a"])

    def test_median_over_twice_the_baseline_median_fails(self):
        within = sb.check_budget([("a", 200), ("b", 400), ("c", 10000)], BASELINE)
        self.assertFalse(within["median_exceeded"])
        over = sb.check_budget([("a", 300), ("b", 500), ("c", 6000)], BASELINE)
        self.assertTrue(over["median_exceeded"])

    def test_rows_without_a_baseline_are_listed_not_judged(self):
        report = sb.check_budget([("new", 99999), ("a", 100)], BASELINE)
        self.assertEqual(report["unbaselined"], ["new"])
        self.assertEqual(report["slow_rows"], [])


class VerdictTests(unittest.TestCase):
    def test_a_clean_pair_of_passes_passes(self):
        passes = [{"gpu/x64": result([row("a"), row("b"), row("c", "skip")])} for _ in range(2)]
        self.assertEqual(sv.verdict(passes, BASELINE, "")["problems"], [])

    def test_row_ok_then_failing_is_degraded(self):
        first = {"gpu/x64": result([row("a"), row("b")])}
        second = {"gpu/x64": result([row("a"), row("b", "fail")])}
        found = sv.verdict([first, second], BASELINE, "")
        self.assertEqual(found["degraded"], ["gpu/x64:b (fail)"])
        self.assertIn("degraded during the run: gpu/x64:b (fail)", found["problems"])

    def test_row_failing_both_times_is_a_failure_not_degraded(self):
        both = {"gpu/x64": result([row("a", "fail")])}
        found = sv.verdict([both, both], BASELINE, "")
        self.assertEqual(found["degraded"], [])
        self.assertEqual(len(found["problems"]), 2)

    def test_not_run_crash_and_vm_incident_fail(self):
        bad = {"gpu/x64": result([row("a", "not_run"), row("b", "crash")], host_crashes=["b"], incidents=[{"evidence": "evidence/001"}])}
        found = sv.verdict([bad, bad], BASELINE, "")
        text = "\n".join(found["problems"])
        self.assertIn("not run", text)
        self.assertIn("suite crash", text)
        self.assertIn("QEMU crash or VM death", text)

    def test_kernel_events_fail_and_ordinary_lines_do_not(self):
        passes = [{"gpu/x64": result([row("a")])} for _ in range(2)]
        quiet = "kernel: usb 1-1: new device\nkernel: amdgpu 0000:03:00.0: ring gfx_0.0.0 timeout\n"
        self.assertEqual(len(sv.kernel_events(quiet)), 1)
        found = sv.verdict(passes, BASELINE, "kernel: NVRM: Xid (PCI:0000:01:00): 31\nkernel: usb 1-1: new device\n")
        self.assertEqual(len(found["kernel_events"]), 1)
        self.assertFalse(sv.verdict(passes, BASELINE, "kernel: usb 1-1: new device\n")["problems"])

    def test_a_missing_adapter_in_the_second_pass_fails(self):
        first = {"gpu1/x64": result([row("a")]), "gpu2/x64": result([row("a")])}
        second = {"gpu1/x64": result([row("a")])}
        self.assertIn("result set differs between the passes: gpu2/x64", sv.verdict([first, second], BASELINE, "")["problems"])

    def test_second_pass_of_x64_only_does_not_miss_x32_results(self):
        first = {"gpu/x64": result([row("a")]), "gpu/x32": result([row("a")]), "sanity/local/gpu/x32": result([row("a")])}
        second = {"gpu/x64": result([row("a", "fail")])}
        found = sv.verdict([first, second], BASELINE, "", "64")
        self.assertEqual(found["degraded"], ["gpu/x64:a (fail)"])
        self.assertFalse(any("differs" in p for p in found["problems"]))
        self.assertTrue(any("differs" in p for p in sv.verdict([first, second], BASELINE, "")["problems"]))

    def test_second_pass_still_fails_on_a_missing_x64_result(self):
        first = {"gpu/x64": result([row("a")]), "gpu/x32": result([row("a")])}
        self.assertIn("result set differs between the passes: gpu/x64", sv.verdict([first, {}], BASELINE, "", "64")["problems"])

    def test_slow_rows_and_summary(self):
        slow = {"gpu/x64": result([row("a", ms=9000), row("b", ms=200)])}
        found = sv.verdict([slow, slow], BASELINE, "")
        self.assertTrue(any(p.startswith("row time a:") for p in found["problems"]))
        text = sv.summary(found)
        self.assertIn("smoke: FAIL", text)
        self.assertLess(len(text.splitlines()), 40)

    def test_load_pass_reads_every_result_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            for rel in ("local/gpu/x64", "sanity/local/gpu/x64"):
                path = pathlib.Path(tmp, rel)
                path.mkdir(parents=True)
                (path / "result.json").write_text(json.dumps(result([row("a")])))
            self.assertEqual(sorted(sv.load_pass(tmp)), ["local/gpu/x64", "sanity/local/gpu/x64"])

    def test_filter_selects_one_suite_in_order(self):
        self.assertEqual(sv.filter_for(["render.a.*", "sanity.b", "render.c"], "render"), "render.a.*,render.c")
        self.assertEqual(sv.filter_for(["render.a.*", "sanity.b"], "sanity"), "sanity.b")

    def test_selection_file_has_both_suites_and_no_blank_globs(self):
        globs = sb.load_selection()
        self.assertTrue(any(g.startswith("render.") for g in globs))
        self.assertTrue(any(g.startswith("sanity.") for g in globs))
        self.assertTrue(all(g.startswith(("render.", "sanity.")) for g in globs))


if __name__ == "__main__":
    unittest.main()
