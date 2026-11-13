import json
import pathlib
import sys
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import suite_status  # noqa: E402


class Clock:
    def __init__(self):
        self.now = 1_800_000_000.0

    def __call__(self):
        return self.now


def begin(planned, label="segment 1 main"):
    return {"label": label, "exe": "lsw-suites64", "bitness": 64, "suites": ["render"], "adapter": {"name": "Adapter A", "vulkan_uuid": "aa"},
            "started": "2027-01-15T08:00:00Z", "phase": "all", "jobs": 2, "planned": planned}


def batch(seq, events=(), done=None, begin_record=None, run="run-1", part="aa-x64", invocation="inv-1", **totals):
    data = {"v": 1, "run": run, "part": part, "invocation": invocation, "seq": seq, "snapshot": begin_record is not None,
            "totals": dict(totals), "events": list(events)}
    if done is not None:
        data["totals"]["done"] = done
    if begin_record is not None:
        data["begin"] = begin_record
    return data


def start(check_id, pid, ago_ms=0):
    return {"t": "start", "id": check_id, "pid": pid, "ago_ms": ago_ms}


def finish(check_id, pid, status="ok", reason="", ms=10, ago_ms=0):
    return {"t": "finish", "id": check_id, "pid": pid, "status": status, "reason": reason, "ms": ms, "ago_ms": ago_ms}


PLAN = ["render.d3d11.clear.a", "render.d3d11.clear.b", "render.vk.copy.a", "render.vk.copy.b"]


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.store = suite_status.Store("here", silent_after=30.0, runs_kept=2, clock=self.clock)

    def only(self):
        return self.store.status()["runs"][0]["parts"][0]["invocations"][0]

    def test_no_report_gives_no_run(self):
        status = self.store.status()
        self.assertEqual(status["runs"], [])
        self.assertIsNone(status["last_report"])

    def test_begin_gives_the_plan_and_events_give_running_checks_and_results(self):
        self.assertEqual(self.store.report(batch(1, begin_record=begin(PLAN), done=0))[0], 200)
        self.assertEqual(self.store.report(batch(2, [start(PLAN[0], 5), start(PLAN[2], 6)], done=0))[0], 200)
        self.clock.now += 7
        self.assertEqual(self.store.report(batch(3, [finish(PLAN[0], 5), start(PLAN[1], 7)], done=1))[0], 200)
        shown = self.only()
        self.assertEqual((shown["planned"], shown["done"], shown["counts"]), (4, 1, {"ok": 1}))
        self.assertEqual([(entry["id"], entry["pid"], entry["seconds"]) for entry in shown["running"]], [(PLAN[2], 6, 7), (PLAN[1], 7, 0)])
        self.assertEqual(shown["apis"], {"d3d11": {"planned": 2, "done": 1, "bad": 0}, "vk": {"planned": 2, "done": 0, "bad": 0}})
        self.assertEqual((shown["state"], shown["label"], shown["bits"]), ("running", "segment 1 main", 64))
        part = self.store.status()["runs"][0]["parts"][0]
        self.assertEqual((part["part"], part["adapter"], part["state"]), ("aa-x64", "Adapter A", "running"))

    def test_results_that_do_not_pass_are_listed_with_their_reason_and_counted_per_api(self):
        self.store.report(batch(1, begin_record=begin(PLAN), done=0))
        events = [finish(PLAN[0], 1, "fail", "pixel differs"), finish(PLAN[2], 2, "crash", "exception 0xC0000005"),
                  finish(PLAN[3], 3, "timeout", "no result after 60 s"), finish(PLAN[1], 4, "skip", "not supported")]
        self.assertEqual(self.store.report(batch(2, events, done=4))[0], 200)
        shown = self.only()
        self.assertEqual(shown["counts"], {"fail": 1, "crash": 1, "timeout": 1, "skip": 1})
        self.assertEqual([(entry["id"], entry["status"], entry["reason"]) for entry in shown["failures"]],
                         [(PLAN[0], "fail", "pixel differs"), (PLAN[2], "crash", "exception 0xC0000005"), (PLAN[3], "timeout", "no result after 60 s")])
        self.assertEqual((shown["apis"]["d3d11"]["bad"], shown["apis"]["vk"]["bad"]), (1, 2))

    def test_an_unknown_part_is_refused_until_the_begin_record_comes_with_everything(self):
        code, answer = self.store.report(batch(7, [finish(PLAN[1], 2)], done=2))
        self.assertEqual(code, 409)
        self.assertEqual(self.store.status()["runs"], [])
        again = batch(8, [finish(PLAN[0], 1, ago_ms=4000), finish(PLAN[1], 2), start(PLAN[2], 3, ago_ms=2500)], done=2, begin_record=begin(PLAN))
        self.assertEqual(self.store.report(again)[0], 200)
        shown = self.only()
        self.assertEqual((shown["done"], shown["planned"]), (2, 4))
        self.assertEqual([(entry["id"], entry["seconds"]) for entry in shown["running"]], [(PLAN[2], 2)])
        self.assertEqual(self.store.report(batch(9, [finish(PLAN[2], 3)], done=3))[0], 200)

    def test_a_missing_report_is_refused_and_the_begin_record_replaces_the_state(self):
        self.store.report(batch(1, begin_record=begin(PLAN), done=0))
        self.store.report(batch(2, [finish(PLAN[0], 1)], done=1))
        self.assertEqual(self.store.report(batch(4, [finish(PLAN[2], 3)], done=3))[0], 409)
        self.assertEqual(self.only()["done"], 1)
        again = batch(5, [finish(PLAN[0], 1), finish(PLAN[1], 2), finish(PLAN[2], 3)], done=3, begin_record=begin(PLAN))
        self.assertEqual(self.store.report(again)[0], 200)
        shown = self.only()
        self.assertEqual((shown["done"], shown["resends"]), (3, 1))

    def test_totals_that_do_not_match_the_received_results_are_refused(self):
        self.store.report(batch(1, begin_record=begin(PLAN), done=0))
        self.assertEqual(self.store.report(batch(2, [finish(PLAN[0], 1)], done=2))[0], 409)
        self.assertEqual(self.store.report(batch(3, [finish(PLAN[1], 2)], done=3))[0], 409)

    def test_a_malformed_report_is_refused(self):
        self.assertEqual(self.store.report([])[0], 400)
        self.assertEqual(self.store.report({"run": "r", "part": "p", "invocation": "i", "seq": "1", "events": []})[0], 400)
        self.assertEqual(self.store.report({"run": "", "part": "p", "invocation": "i", "seq": 1, "events": []})[0], 400)

    def test_the_rate_counts_the_results_of_the_last_five_minutes(self):
        self.store.report(batch(1, begin_record=begin(PLAN * 50), done=0))
        self.clock.now += 60
        self.store.report(batch(2, [finish(PLAN[0], index) for index in range(30)], done=30))
        self.assertEqual(self.only()["checks_per_minute"], 30.0)
        self.clock.now += 540
        self.store.report(batch(3, [finish(PLAN[1], index, ago_ms=1000 * index) for index in range(10)], done=40))
        self.assertEqual(self.only()["checks_per_minute"], 2.0)

    def test_a_part_without_a_report_is_silent_since_its_last_report_and_running_again_when_one_comes(self):
        self.store.report(batch(1, begin_record=begin(PLAN), done=0))
        self.store.report(batch(2, [start(PLAN[0], 1)], done=0))
        last = suite_status.clock_text(self.clock.now)
        self.clock.now += 29
        self.assertEqual(self.only()["state"], "running")
        self.clock.now += 2
        shown = self.only()
        self.assertEqual((shown["state"], shown["silent_since"], shown["seconds_since_report"]), ("silent", last, 31))
        run = self.store.status()["runs"][0]
        self.assertEqual((run["state"], run["parts"][0]["state"], run["parts"][0]["silent_since"]), ("silent", "silent", last))
        self.store.report(batch(3, [], done=0))
        self.assertEqual((self.only()["state"], self.only()["silent_since"]), ("running", None))

    def test_the_end_record_finishes_the_invocation_and_it_never_turns_silent(self):
        self.store.report(batch(1, begin_record=begin(PLAN[:1]), done=0))
        end = {"t": "end", "ago_ms": 0, "complete": True, "crashed": 0, "timed_out": 0, "failed": 0}
        self.store.report(batch(2, [start(PLAN[0], 1), finish(PLAN[0], 1), end], done=1))
        self.clock.now += 3600
        shown = self.only()
        self.assertEqual((shown["state"], shown["end"]["complete"], shown["running"]), ("finished", True, []))
        self.assertEqual(self.store.status()["runs"][0]["state"], "finished")

    def test_segments_are_separate_invocations_of_one_part_and_bitness_is_another_part(self):
        self.store.report(batch(1, begin_record=begin(PLAN[:2], "segment 1 main"), done=0))
        self.store.report(batch(1, begin_record=begin(PLAN[2:], "segment 2 isolated"), done=0, invocation="inv-2"))
        self.store.report(batch(1, begin_record=dict(begin(PLAN), bitness=32), done=0, part="aa-x32", invocation="inv-3"))
        parts = self.store.status()["runs"][0]["parts"]
        self.assertEqual([part["part"] for part in parts], ["aa-x32", "aa-x64"])
        self.assertEqual([(inv["label"], inv["planned"]) for inv in parts[1]["invocations"]], [("segment 1 main", 2), ("segment 2 isolated", 2)])
        self.assertEqual(parts[0]["bits"], 32)

    def test_finished_runs_stay_and_only_the_latest_are_kept(self):
        for index in range(3):
            self.clock.now += 100
            self.store.report(batch(1, begin_record=begin(PLAN), done=0, run=f"run-{index}"))
        self.assertEqual([run["run"] for run in self.store.status()["runs"]], ["run-2", "run-1"])


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.store = suite_status.Store("here")
        other = suite_status.Store("there")
        other.report(batch(1, begin_record=begin(PLAN), done=0, run="their-run"))
        self.other_server = suite_status.make_server("127.0.0.1", 0, other, suite_status.Peers([]))
        peers = [suite_status.Peer("there", f"http://127.0.0.1:{self.other_server.server_address[1]}/status"),
                 suite_status.Peer("gone", "http://127.0.0.1:9/status")]
        self.server = suite_status.make_server("127.0.0.1", 0, self.store, suite_status.Peers(peers))
        self.base = f"http://127.0.0.1:{self.server.server_address[1]}"
        for server in (self.server, self.other_server):
            threading.Thread(target=server.serve_forever, daemon=True).start()

    def tearDown(self):
        for server in (self.server, self.other_server):
            server.shutdown()
            server.server_close()

    def post(self, path, data):
        request = urllib.request.Request(self.base + path, data=json.dumps(data).encode(), headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=5) as answer:
                return answer.status
        except urllib.error.HTTPError as error:
            error.close()
            return error.code

    def get(self, path):
        with urllib.request.urlopen(self.base + path, timeout=10) as answer:
            return answer.read()

    def test_reports_posted_to_the_server_show_in_its_status(self):
        self.assertEqual(self.post("/report", batch(2, [finish(PLAN[0], 1)], done=1)), 409)
        self.assertEqual(self.post("/report", batch(3, [finish(PLAN[0], 1)], done=1, begin_record=begin(PLAN))), 200)
        self.assertEqual(self.post("/report", batch(4, [start(PLAN[1], 2)], done=1)), 200)
        status = json.loads(self.get("/status"))
        shown = status["runs"][0]["parts"][0]["invocations"][0]
        self.assertEqual((status["machine"], status["reports"], shown["done"], shown["running"][0]["id"]), ("here", 3, 1, PLAN[1]))

    def test_a_report_to_another_path_or_without_json_is_refused(self):
        self.assertEqual(self.post("/status", batch(1, begin_record=begin(PLAN))), 404)
        request = urllib.request.Request(self.base + "/report", data=b"not json")
        with self.assertRaises(urllib.error.HTTPError) as refused:
            urllib.request.urlopen(request, timeout=5)
        refused.exception.close()
        self.assertEqual(refused.exception.code, 400)

    def test_machines_lists_this_machine_then_each_other_machine_or_why_it_is_not_reachable(self):
        machines = json.loads(self.get("/api/machines"))["machines"]
        self.assertEqual([machine["name"] for machine in machines], ["here", "there", "gone"])
        self.assertEqual(machines[1]["status"]["runs"][0]["run"], "their-run")
        self.assertNotIn("status", machines[2])
        self.assertTrue(machines[2]["error"])

    def test_the_page_is_served_and_asks_for_the_machines(self):
        page = self.get("/").decode()
        self.assertIn("api/machines", page)
        self.assertNotIn("http://", page.split("<script>")[1])


class HostsFileTests(unittest.TestCase):
    def test_other_machines_come_from_the_hosts_file_and_this_machine_is_the_local_line(self):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix="-hosts") as hosts:
            hosts.write("# name target\nmine local\nfar someone@far-machine disk-cache=none\n")
            hosts.flush()
            direct = suite_status.peers_from_hosts(hosts.name, 5190, False, "unused")
            tunnelled = suite_status.peers_from_hosts(hosts.name, 5190, True, "/control")
            self.assertEqual(suite_status.own_name(hosts.name), "mine")
        self.assertEqual([(peer.name, peer.url, peer.forward) for peer in direct], [("far", "http://far-machine:5190/status", None)])
        self.assertEqual([(peer.name, peer.url, peer.forward) for peer in tunnelled],
                         [("far", "http://127.0.0.1:5191/status", ("/control", "someone@far-machine", 5191, 5190))])


if __name__ == "__main__":
    unittest.main()
