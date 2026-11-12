import pathlib
import sys
import unittest
import xml.etree.ElementTree as ET
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gpu_matrix  # noqa: E402

DOMAIN = """<domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <name>lsw</name>
  <on_crash>restart</on_crash>
  {pm}
  <devices>
    <emulator>/usr/bin/qemu</emulator>
    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2' cache='none' io='native' discard='unmap' detect_zeroes='unmap'/>
      <source file='/vm/disk.qcow2'/>
    </disk>
    <disk type='file' device='cdrom'>
      <driver name='qemu' type='raw' cache='none' io='native'/>
      <source file='/vm/install.iso'/>
    </disk>
    <disk type='file' device='disk'>
      <driver name='qemu' type='vpc' cache='none' io='native'/>
      <source file='/vm/other.vhd'/>
    </disk>
  </devices>
  <qemu:commandline>
    <qemu:arg value='-device'/>
    <qemu:arg value='virtio-gpu-av-pci,id=lsw-gpu,host-gpu=x'/>
  </qemu:commandline>
</domain>"""


class Gpu:
    uuid = "0123"


def retargeted(pm):
    with mock.patch.object(gpu_matrix, "emulator_path", return_value=pathlib.Path("/qemu")):
        return ET.fromstring(gpu_matrix.retarget(DOMAIN.format(pm=pm), [Gpu()]))


def retargeted_text(**options):
    with mock.patch.object(gpu_matrix, "emulator_path", return_value=pathlib.Path("/qemu")):
        return gpu_matrix.retarget(DOMAIN.format(pm=""), [Gpu()], **options)


def canonical(text, relaxed=()):
    root = ET.fromstring(text)
    for driver in root.iter("driver"):
        for name in relaxed:
            driver.attrib.pop(name, None)
    return ET.canonicalize(ET.tostring(root, encoding="unicode"), strip_text=True)


class DiskCache(unittest.TestCase):
    def drivers(self, text):
        return [dict(driver.attrib) for driver in ET.fromstring(text).iter("driver")]

    def test_no_setting_leaves_the_disks_identical(self):
        self.assertEqual(self.drivers(retargeted_text()), self.drivers(DOMAIN.format(pm="")))

    def test_writeback_changes_only_cache_and_io_of_the_system_disk(self):
        text = retargeted_text(disk_cache="writeback")
        before = self.drivers(DOMAIN.format(pm=""))
        after = self.drivers(text)
        self.assertEqual(after[0], dict(before[0], cache="writeback", io="threads"))
        self.assertEqual(after[1:], before[1:])
        self.assertEqual(canonical(text, ("cache", "io")), canonical(retargeted_text(), ("cache", "io")))

    def test_mode_none_changes_nothing(self):
        self.assertEqual(canonical(retargeted_text(disk_cache="none")), canonical(retargeted_text()))

    def test_directsync_keeps_native_io(self):
        self.assertEqual(self.drivers(retargeted_text(disk_cache="directsync"))[0]["io"], "native")

    def test_unsafe_and_unknown_modes_are_errors(self):
        for mode in ("unsafe", "bogus", ""):
            with self.assertRaises(gpu_matrix.RunFailed):
                retargeted_text(disk_cache=mode)

    def test_hybrid_roles_are_kept(self):
        with mock.patch.object(gpu_matrix, "emulator_path", return_value=pathlib.Path("/qemu")):
            text = gpu_matrix.retarget(DOMAIN.format(pm=""), [Gpu(), Gpu()], disk_cache="writeback")
        self.assertIn("hybrid=integrated", text)
        self.assertIn("hybrid=discrete", text)


class PowerManagement(unittest.TestCase):
    def check(self, root):
        found = root.findall("./pm")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].find("suspend-to-mem").get("enabled"), "no")
        self.assertEqual(found[0].find("suspend-to-disk").get("enabled"), "no")
        self.assertEqual(root.findall(".//pm"), found)

    def test_missing_element_is_added_to_the_domain(self):
        self.check(retargeted(""))

    def test_existing_element_is_replaced(self):
        root = retargeted("<pm><suspend-to-mem enabled='yes'/><suspend-to-disk enabled='yes'/></pm>")
        self.check(root)

    def test_element_comes_before_the_devices(self):
        names = [child.tag for child in retargeted("")]
        self.assertLess(names.index("pm"), names.index("devices"))


if __name__ == "__main__":
    unittest.main()
