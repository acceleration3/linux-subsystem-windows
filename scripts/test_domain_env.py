import pathlib
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import domain_env  # noqa: E402

NS = "{http://libvirt.org/schemas/domain/qemu/1.0}"
DOMAIN = """<domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <name>lsw</name>
  <qemu:commandline>
    <qemu:arg value='-device'/>
    <qemu:env name='KEEP' value='1'/>
    <qemu:env name='SET' value='old'/>
  </qemu:commandline>
</domain>"""

HYBRID = """<domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <name>lsw</name>
  <qemu:commandline>
    <qemu:arg value='-device'/>
    <qemu:arg value='virtio-gpu-av-pci,id=lsw-gpu,bus=pcie.0,addr=0x11,max_outputs=1,hybrid=integrated'/>
    <qemu:arg value='-device'/>
    <qemu:arg value='virtio-gpu-av-pci,id=lsw-gpu-1,bus=pcie.0,addr=0x12,display=off,hybrid=discrete'/>
    <qemu:env name='KEEP' value='1'/>
  </qemu:commandline>
</domain>"""


def arguments(text):
    return [arg.get("value") for arg in ET.fromstring(text).find(f"{NS}commandline").findall(f"{NS}arg")]


def environment(text):
    root = ET.fromstring(text)
    return {env.get("name"): env.get("value") for env in root.find(f"{NS}commandline").findall(f"{NS}env")}


class WithEnvironment(unittest.TestCase):
    def test_adds_and_replaces(self):
        result = environment(domain_env.with_environment(DOMAIN, ["SET=new", "ADDED=a=b"]))
        self.assertEqual(result, {"KEEP": "1", "SET": "new", "ADDED": "a=b"})

    def test_source_text_is_not_changed(self):
        domain_env.with_environment(DOMAIN, ["ADDED=1"])
        self.assertEqual(environment(DOMAIN), {"KEEP": "1", "SET": "old"})

    def test_hybrid_roles_of_both_adapters_are_kept(self):
        result = domain_env.with_environment(HYBRID, ["ADDED=1"])
        self.assertEqual(arguments(result), arguments(HYBRID))
        devices = [arg for arg in arguments(result) if arg.startswith("virtio-gpu-av-pci")]
        self.assertEqual([device.rsplit(",", 1)[1] for device in devices], ["hybrid=integrated", "hybrid=discrete"])
        self.assertEqual(environment(result), {"KEEP": "1", "ADDED": "1"})

    def test_rejects_a_pair_without_value(self):
        with self.assertRaises(ValueError):
            domain_env.with_environment(DOMAIN, ["NAME"])

    def test_rejects_a_domain_without_commandline(self):
        with self.assertRaises(ValueError):
            domain_env.with_environment("<domain/>", ["A=1"])

    def test_command_line_writes_the_file(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "in.xml"
            target = pathlib.Path(directory) / "out.xml"
            source.write_text(DOMAIN)
            self.assertEqual(domain_env.main(["x", str(source), str(target), "A=1"]), 0)
            self.assertEqual(environment(target.read_text())["A"], "1")
            self.assertEqual(domain_env.main(["x", str(source), str(target), "bad"]), 1)


if __name__ == "__main__":
    unittest.main()
