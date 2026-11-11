import importlib.util
import pathlib
import struct
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("render_audit", HERE / "render-audit.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)

TOLERANCE = {"name": "filter(4)", "byte_delta": 4}


def write_rref(path, values, width=4, height=1):
    header = b"LSWR" + struct.pack("<IIII", 1, width, height, 8) + b"\0" * 12
    path.write_bytes(header + bytes(values))


def raw(values, width=4, height=1):
    return audit.Raw(width, height, "r8", audit.array.array("d", values), bytes(values))


class Bracket(unittest.TestCase):
    def test_a_value_between_the_lod_renderings_replaces_the_oracle(self):
        out = audit.bracketed(raw([100, 100, 100, 100]), raw([90, 90, 90, 90]), raw([120, 120, 120, 120]),
                              raw([100, 110, 130, 60]), audit.normalize_tolerance(TOLERANCE))
        self.assertEqual(list(out.data), [100, 110, 100, 100])

    def test_the_audit_judge_reads_the_dumped_bracket(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            write_rref(folder / "f000.oracle.rref", [100, 100, 100, 100])
            write_rref(folder / "f000.actual.rref", [100, 110, 112, 100])
            write_rref(folder / "f000.low.rref", [90, 90, 90, 90])
            write_rref(folder / "f000.high.rref", [120, 120, 120, 120])
            capture = {"frame": 0, "label": "", "bracket": True}
            tol = audit.normalize_tolerance(TOLERANCE)
            actual = audit.read_rref(str(folder / "f000.actual.rref"))
            oracle = audit.read_rref(str(folder / "f000.oracle.rref"))
            self.assertTrue(audit.judge(audit.adjusted_oracle(oracle, actual, tol, directory, capture), actual, tol)["pass"])
            self.assertFalse(audit.judge(audit.adjusted_oracle(oracle, actual, tol, directory, {"frame": 0, "label": ""}), actual, tol)["pass"])
            (folder / "f000.high.rref").unlink()
            with self.assertRaises(OSError):
                audit.adjusted_oracle(oracle, actual, tol, directory, capture)


if __name__ == "__main__":
    unittest.main()
