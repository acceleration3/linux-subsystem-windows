import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import stack_manifest as sm

MAPS = """\
7f0000000000-7f0000100000 r--p 00000000 08:01 11 /home/u/stack/libav_executor.so
7f0000100000-7f0000200000 r-xp 00100000 08:01 11 /home/u/stack/libav_executor.so
7f0000300000-7f0000400000 r--p 00000000 08:01 12 /home/u/stack/qemu-modules/hw-display-virtio-gpu-av.so (deleted)
7f0000500000-7f0000600000 r--p 00000000 08:01 13 /usr/lib/libc.so.6
"""


def build_stack(root, executor=b"E", module=b"offer_hybrid M", pci=b"P"):
    root = pathlib.Path(root)
    (root / "qemu-modules").mkdir()
    (root / "av-drivers" / "package").mkdir(parents=True)
    (root / "av-drivers" / "package" / "package.sha256").write_text("x")
    (root / "libav_executor.so").write_bytes(executor)
    (root / "qemu-modules" / "hw-display-virtio-gpu-av.so").write_bytes(module)
    (root / "qemu-modules" / "hw-display-virtio-gpu-pci-av.so").write_bytes(pci)


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        build_stack(self.temp.name)
        self.manifest = sm.make_manifest(self.temp.name, "d" * 40, False, "s" * 40)

    def tearDown(self):
        self.temp.cleanup()

    def test_manifest_records_revisions_hybrid_and_hashes(self):
        self.assertEqual(self.manifest["driver_sha"], "d" * 40)
        self.assertTrue(self.manifest["hybrid"])
        self.assertEqual(set(self.manifest["files"]), set(sm.KEY_FILES))

    def test_mapped_regions_take_the_first_mapping_and_ignore_deleted_marker(self):
        regions = sm.mapped_regions(MAPS)
        self.assertEqual(regions["libav_executor.so"][0], "7f0000000000-7f0000100000")
        self.assertEqual(regions["hw-display-virtio-gpu-av.so"][1], "/home/u/stack/qemu-modules/hw-display-virtio-gpu-av.so")
        self.assertNotIn("libc.so.6", regions)

    def test_matching_vm_passes(self):
        mapped = {name.rsplit("/", 1)[-1]: md5 for name, md5 in self.manifest["files"].items()}
        self.assertEqual(sm.check(self.manifest, mapped), [])

    def test_stale_executor_and_missing_module_are_reported(self):
        mapped = {name.rsplit("/", 1)[-1]: md5 for name, md5 in self.manifest["files"].items()}
        mapped["libav_executor.so"] = "0" * 32
        del mapped["hw-display-virtio-gpu-pci-av.so"]
        problems = sm.check(self.manifest, mapped)
        self.assertEqual(len(problems), 2)
        self.assertIn("libav_executor.so", problems[0])
        self.assertIn("not mapped", problems[1])

    def test_report_reads_the_mapping_before_the_path(self):
        seen = []

        def reader(path):
            seen.append(path)
            if "map_files" in path:
                raise OSError
            return "m"

        result = sm.report(1, MAPS, reader)
        self.assertEqual(result, {"libav_executor.so": "m", "hw-display-virtio-gpu-av.so": "m"})
        self.assertEqual(seen[0], "/proc/1/map_files/7f0000000000-7f0000100000")


if __name__ == "__main__":
    unittest.main()
