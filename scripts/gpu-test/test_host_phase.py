import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import host_phase  # noqa: E402
from host_phase import Config, ROWS  # noqa: E402

DEVICE = """module/0 devices=1 lendings={lendings} lent_pages=0
module/0/device/0 realized=true gpu=1 capsets=1 blobs={blobs} scanouts=0 host_window_bytes=8589934592 contexts={contexts} driver_ok=true
{blob_lines}
module/0/device/0/contexts/0/context/257 name=app batches=3
module/0/device/0/window_stats/0 attached=true stable=true total_pages=100 pages_in_use={pages} peak_pages_in_use={peak} refusals=0 refused_bytes=0 largest_free_run_at_refusal=0
"""
EXECUTOR = """executor/0 sessions=64 live={live} gpus=1 overflowed=false
executor/0/session/0 api=d3d gpu=1 domain=1{pid}
executor/0/session/0/channel/257 reports={reports} rejected={rejected} refused_other={refused} refused_no_memory=0 arena_bytes=131072{extra}
"""


def blob_lines(count, size=4 << 20):
    return "\n".join(f"module/0/device/0/host_blobs/0/host_blob/{1000 + i} context=257 size={size} caching=1 mapped=true"
                     for i in range(count))


def snapshot(blobs=2, pages=10, peak=10, live=1, reports=5, rejected=0, refused=0, extra="", pid="", pids=None,
             answered=True, latency=20.0, device_start=100, rss=100 << 20, threads=30, fds=40, gpu=1 << 30, lendings=0,
             qemu_start=7, tail=""):
    device = DEVICE.format(blobs=blobs, contexts=1, pages=pages, peak=peak, lendings=lendings,
                           blob_lines=blob_lines(blobs)) + tail
    executor = EXECUTOR.format(live=live, reports=reports, rejected=rejected, refused=refused, extra=extra, pid=pid)
    summary = host_phase.summarize(host_phase.parse_state(device), host_phase.parse_state(executor))
    process = {"alive": True, "start": device_start, "threads": threads, "fds": fds, "rss": rss}
    return {"time": 0, "modules": {"device": [{"pid": 11, "process": process, "answered": answered, "latency_ms": latency,
                                               "text": device if answered else "error x"}],
                                   "executor": [{"pid": 11, "process": dict(process, start=device_start),
                                                 "answered": answered, "latency_ms": latency, "text": executor}]},
            "qemu": {5: {"alive": True, "start": qemu_start}}, "gpu_memory": gpu, "summary": summary}


def verdicts(before, after, **run):
    return {v["id"]: v for v in host_phase.judge(before, after, run)}


class Parsing(unittest.TestCase):
    def test_summary_counts_objects(self):
        text = DEVICE.format(blobs=3, contexts=1, pages=7, peak=9, lendings=2, blob_lines=blob_lines(3))
        summary = host_phase.summarize(host_phase.parse_state(text), [])
        device = summary["device"]
        self.assertEqual((device["blobs"], device["blob_bytes"], device["contexts"], device["lendings"]), (3, 3 * (4 << 20), 1, 2))
        self.assertEqual((device["pages_in_use"], device["peak_pages_in_use"]), (7, 9))

    def test_executor_counters_and_pids(self):
        text = EXECUTOR.format(live=2, reports=9, rejected=1, refused=0, extra=" transferred_bytes=4096", pid=" pid=4242")
        executor = host_phase.summarize([], host_phase.parse_state(text))["executor"]
        self.assertEqual((executor["live_sessions"], executor["channels"], executor["rejected"], executor["reports"]), (2, 1, 1, 9))
        self.assertEqual(executor["counters"]["transferred_bytes"], 4096)
        self.assertEqual(executor["session_pids"], {4242})


class Judging(unittest.TestCase):
    def test_clean_run_passes_and_unknowns_are_skipped(self):
        found = verdicts(snapshot(), snapshot(), logs={"avgpu": "info started\n", "qemu": "", "kernel": "usb 1-1: new device\n"}, stalls=[])
        self.assertEqual([v for v in found.values() if v["status"] == "fail"], [])
        self.assertEqual(set(found), set(ROWS))
        for row in ROWS[7:]:
            self.assertEqual(found[row]["status"], "skip")
        self.assertIn("bytes-transferred counter", found[ROWS[7]]["detail"])

    def test_restarted_device_fails_alive(self):
        found = verdicts(snapshot(), snapshot(device_start=999), logs={"kernel": ""}, stalls=[])
        self.assertEqual(found[ROWS[0]]["status"], "fail")
        self.assertIn("restarted", found[ROWS[0]]["detail"])

    def test_dead_qemu_fails_alive(self):
        after = snapshot()
        after["qemu"] = {}
        found = verdicts(snapshot(), after, logs={"kernel": ""}, stalls=[])
        self.assertIn("QEMU 5", found[ROWS[0]]["detail"])

    def test_slow_or_silent_stack_fails_responsive(self):
        slow = verdicts(snapshot(), snapshot(latency=9000.0), logs={"kernel": ""}, stalls=[])
        self.assertEqual(slow[ROWS[1]]["status"], "fail")
        silent = verdicts(snapshot(), snapshot(answered=False), logs={"kernel": ""}, stalls=[])
        self.assertEqual(silent[ROWS[1]]["status"], "fail")

    def test_leaked_blobs_fail_objects_and_memory(self):
        found = verdicts(snapshot(blobs=2), snapshot(blobs=40), logs={"kernel": ""}, stalls=[])
        self.assertEqual(found[ROWS[3]]["status"], "fail")
        self.assertIn("blobs is 40", found[ROWS[3]]["detail"])
        self.assertEqual(found[ROWS[2]]["status"], "fail")

    def test_growth_inside_tolerance_passes(self):
        found = verdicts(snapshot(blobs=2), snapshot(blobs=4, rss=120 << 20, threads=34, fds=50), logs={"kernel": ""}, stalls=[])
        for row in ROWS[2:5]:
            self.assertEqual(found[row]["status"], "ok", found[row])

    def test_host_gpu_memory_and_threads(self):
        found = verdicts(snapshot(), snapshot(gpu=3 << 30, threads=80, fds=400), logs={"kernel": ""}, stalls=[])
        self.assertIn("host GPU memory", found[ROWS[2]]["detail"])
        self.assertIn("more threads", found[ROWS[4]]["detail"])
        self.assertIn("more open files", found[ROWS[4]]["detail"])

    def test_gpu_fault_lines_fail_errors(self):
        kernel = ("Nov 07 10:00:01 host kernel: amdgpu 0000:03:00.0: amdgpu: [gfxhub] page fault (src_id:0 ring:24 vmid:8)\n"
                  "Nov 07 10:00:02 host kernel: amdgpu 0000:03:00.0: amdgpu: GPU reset begin!\n")
        found = verdicts(snapshot(), snapshot(), logs={"kernel": kernel}, stalls=[])
        self.assertEqual(found[ROWS[5]]["status"], "fail")
        self.assertIn("2 host GPU fault or reset", found[ROWS[5]]["detail"])

    def test_other_gpu_vendors_are_recognised(self):
        for line in ("i915 0000:00:02.0: [drm] GPU HANG: ecode 12:1:85dffffb", "xe 0000:03:00.0: [drm] GT0: GPU reset",
                     "NVRM: Xid (PCI:0000:01:00): 31, pid=1", "nvidia 0000:01:00.0: GPU fault detected"):
            found = verdicts(snapshot(), snapshot(), logs={"kernel": line + "\n"}, stalls=[])
            self.assertEqual(found[ROWS[5]]["status"], "fail", line)

    def test_unrelated_kernel_lines_pass(self):
        kernel = "kernel: usb 3-1: reset high-speed USB device number 4\nkernel: nvme nvme0: resetting controller\n"
        found = verdicts(snapshot(), snapshot(), logs={"kernel": kernel}, stalls=[])
        self.assertEqual(found[ROWS[5]]["status"], "ok")

    def test_unreadable_kernel_log_fails_instead_of_passing_silently(self):
        found = verdicts(snapshot(), snapshot(), logs={"kernel": None}, stalls=[])
        self.assertEqual(found[ROWS[5]]["status"], "fail")
        self.assertIn("cannot be judged", found[ROWS[5]]["detail"])

    def test_log_errors_and_counters_fail_errors(self):
        found = verdicts(snapshot(), snapshot(rejected=3), logs={"avgpu": "ok\nexecutor: fatal: bad report\n",
                                                                 "qemu": "d3d9: device 4 is removed\n", "kernel": ""}, stalls=[])
        detail = found[ROWS[5]]["detail"]
        self.assertIn("avgpu.log", detail)
        self.assertIn("qemu log", detail)
        self.assertIn("rejected rose by 3", detail)

    def test_benign_lines_are_ignored(self):
        config = Config(benign=("expected oom",))
        found = {v["id"]: v for v in host_phase.judge(snapshot(), snapshot(), {"logs": {"avgpu": "error: expected oom\n", "kernel": ""},
                                                                              "stalls": []}, config)}
        self.assertEqual(found[ROWS[5]]["status"], "ok")

    def test_stalls(self):
        stalled = [{"module": "device", "pid": 11, "thread": 4, "frames": ["a", "b", "wait_fence"]}]
        found = verdicts(snapshot(), snapshot(), logs={"kernel": ""}, stalls=stalled)
        self.assertEqual(found[ROWS[6]]["status"], "fail")
        self.assertIn("wait_fence", found[ROWS[6]]["detail"])
        self.assertEqual(verdicts(snapshot(), snapshot(), logs={"kernel": ""}, stalls=None)[ROWS[6]]["status"], "skip")

    def test_accounting_with_counters(self):
        before = snapshot(extra=" transferred_bytes=0", reports=0)
        after = snapshot(extra=" transferred_bytes=1000", reports=100)
        ok = verdicts(before, after, logs={"kernel": ""}, stalls=[], guest={"bytes_verified": 800, "submissions": 90})
        self.assertEqual((ok[ROWS[7]]["status"], ok[ROWS[8]]["status"]), ("ok", "ok"))
        low = verdicts(before, after, logs={"kernel": ""}, stalls=[], guest={"bytes_verified": 5000, "submissions": 90})
        self.assertEqual(low[ROWS[7]]["status"], "fail")
        high = verdicts(before, after, logs={"kernel": ""}, stalls=[], guest={"bytes_verified": 10, "submissions": 90})
        self.assertIn("more than", high[ROWS[7]]["detail"])
        far = verdicts(before, after, logs={"kernel": ""}, stalls=[], guest={"bytes_verified": 800, "submissions": 10})
        self.assertEqual(far[ROWS[8]]["status"], "fail")

    def test_processes_agree(self):
        after = snapshot(pid=" pid=4242")
        ok = verdicts(snapshot(), after, logs={"kernel": ""}, stalls=[], guest={"pids": {4242}})
        self.assertEqual(ok[ROWS[9]]["status"], "ok")
        missing = verdicts(snapshot(), after, logs={"kernel": ""}, stalls=[], guest={"pids": {4242, 77}})
        self.assertEqual(missing[ROWS[9]]["status"], "fail")
        self.assertIn("77", missing[ROWS[9]]["detail"])

    def test_peak_covers(self):
        after = snapshot(peak=1000)
        ok = verdicts(snapshot(), after, logs={"kernel": ""}, stalls=[], guest={"held_peak_bytes": 4096 * 900})
        self.assertEqual(ok[ROWS[10]]["status"], "ok")
        low = verdicts(snapshot(), after, logs={"kernel": ""}, stalls=[], guest={"held_peak_bytes": 4096 * 2000})
        self.assertEqual(low[ROWS[10]]["status"], "fail")

    def test_shutdown(self):
        stop = {"exit_status": {"device": 0, "executor": 1}, "stale": ["/dev/shm/av-state-device-11.sock"]}
        found = verdicts(snapshot(), snapshot(), logs={"kernel": ""}, stalls=[], stop=stop)
        self.assertEqual(found[ROWS[11]]["status"], "fail")
        self.assertIn("executor exited with status 1", found[ROWS[11]]["detail"])
        self.assertEqual(verdicts(snapshot(), snapshot(), logs={"kernel": ""}, stalls=[], stop={"exit_status": {"device": 0}, "stale": []})[ROWS[11]]["status"], "ok")

    def test_matrix_folding(self):
        found = host_phase.judge(snapshot(), snapshot(device_start=1), {"logs": {"kernel": ""}, "stalls": []})
        failures, notes = host_phase.to_matrix(found)
        self.assertEqual([f["case"] for f in failures], [ROWS[0]])
        self.assertEqual(len(notes), 5)


class Probing(unittest.TestCase):
    def test_process_reads_proc_files(self):
        with tempfile.TemporaryDirectory() as directory:
            proc = pathlib.Path(directory, "proc")
            (proc / "11" / "fd").mkdir(parents=True)
            for name in ("0", "1", "2"):
                (proc / "11" / "fd" / name).write_text("")
            fields = ["S"] + ["0"] * 18 + ["4242"] + ["0"] * 5
            (proc / "11" / "stat").write_text("11 (av device) " + " ".join(fields))
            (proc / "11" / "status").write_text("Name:\tx\nThreads:\t17\nVmRSS:\t2048 kB\n")
            info = host_phase.StackProbe(shm=directory, proc=proc).process(11)
            self.assertEqual((info["alive"], info["start"], info["threads"], info["fds"], info["rss"]),
                             (True, 4242, 17, 3, 2048 * 1024))
            self.assertFalse(host_phase.StackProbe(shm=directory, proc=proc).process(99)["alive"])

    def test_settle_stops_once_steady_and_returns_the_last_snapshot(self):
        class Fake:
            def __init__(self):
                self.calls = 0

            def snapshot(self, work=None):
                self.calls += 1
                return {"summary": {"n": min(self.calls, 3)}}

        clock = {"t": 0.0}
        probe = Fake()
        last = host_phase.settled(probe, None, Config(settle_seconds=20.0), sleep=lambda s: clock.update(t=clock["t"] + s),
                                  clock=lambda: clock["t"])
        self.assertEqual(last["summary"]["n"], 3)
        self.assertLessEqual(clock["t"], 20.0)

    def test_guest_metrics_from_result_files(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory, "r.json")
            path.write_text(json.dumps({"checks": [{"pid": 5, "measures": {"bytes_verified": 100, "submissions": 3}},
                                                   {"pid": 6, "measures": {"bytes_verified": 50, "held_peak_bytes": 900}}]}))
            metrics = host_phase.guest_metrics([path, pathlib.Path(directory, "missing.json")])
            self.assertEqual((metrics["pids"], metrics["bytes_verified"], metrics["submissions"], metrics["held_peak_bytes"]),
                             ({5, 6}, 150, 3, 900))

    def test_log_window_reads_only_what_came_after_the_mark(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory, "avgpu.log")
            path.write_text("old line\n")
            window = host_phase.LogWindow(path)
            with open(path, "a") as handle:
                handle.write("new line\n")
            self.assertEqual(window.delta(), "new line\n")
            self.assertEqual(host_phase.LogWindow(None).delta(), "")


if __name__ == "__main__":
    unittest.main()
