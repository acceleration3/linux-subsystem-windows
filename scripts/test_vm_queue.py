import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import vm_queue

TOOL = str(pathlib.Path(__file__).parent / "vm_queue.py")


def sleeper(seconds=60):
    return subprocess.Popen(["sleep", str(seconds)])


class QueueTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.queue = vm_queue.Queue(self.temp.name, "local")
        self.children = []

    def tearDown(self):
        for child in self.children:
            child.kill()
            child.wait()
        self.temp.cleanup()

    def process(self):
        child = sleeper()
        self.children.append(child)
        return child

    def test_first_come_is_first(self):
        a, b, c = (self.process() for _ in range(3))
        tickets = [self.queue.join(pid=p.pid) for p in (a, b, c)]
        self.assertEqual([self.queue.position(t) for t in tickets], [0, 1, 2])

    def test_a_dead_process_loses_its_place(self):
        a, b = self.process(), self.process()
        first, second = self.queue.join(pid=a.pid), self.queue.join(pid=b.pid)
        a.kill()
        a.wait()
        self.assertEqual(self.queue.position(second), 0)
        self.assertFalse(first.path.exists())

    def test_a_reused_pid_is_not_the_old_ticket(self):
        a = self.process()
        ticket = self.queue.join(pid=a.pid)
        ticket.data["start"] = "1"
        ticket.path.write_text(__import__("json").dumps(ticket.data))
        self.assertEqual(self.queue.live(), [])

    def test_leaving_passes_the_turn_on(self):
        a, b = self.process(), self.process()
        first, second = self.queue.join(pid=a.pid), self.queue.join(pid=b.pid)
        self.queue.leave(first)
        self.assertEqual(self.queue.position(second), 0)

    def test_hold_runs_the_command_and_releases(self):
        result = subprocess.run([sys.executable, TOOL, "hold", "--host", "local", "--queue-dir", self.temp.name,
                                 "--poll", "0.1", "--", "sh", "-c", "exit 7"], capture_output=True)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(self.queue.live(), [])

    def test_hold_waits_behind_a_holder_and_goes_after_it(self):
        holder = self.process()
        ticket = self.queue.join(pid=holder.pid)
        marker = pathlib.Path(self.temp.name) / "ran"
        waiter = subprocess.Popen([sys.executable, TOOL, "hold", "--host", "local", "--queue-dir", self.temp.name,
                                   "--poll", "0.1", "--", "touch", str(marker)])
        time.sleep(1)
        self.assertFalse(marker.exists())
        self.queue.leave(ticket)
        self.assertEqual(waiter.wait(timeout=10), 0)
        self.assertTrue(marker.exists())

    def test_hold_releases_when_signalled(self):
        waiter = subprocess.Popen([sys.executable, TOOL, "hold", "--host", "local", "--queue-dir", self.temp.name,
                                   "--poll", "0.1", "--", "sleep", "60"])
        time.sleep(1)
        self.assertEqual(len(self.queue.live()), 1)
        waiter.send_signal(signal.SIGTERM)
        waiter.wait(timeout=10)
        self.assertEqual(self.queue.live(), [])


if __name__ == "__main__":
    unittest.main()
