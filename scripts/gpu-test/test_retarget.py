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
