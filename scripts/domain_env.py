#!/usr/bin/env python3
"""Add NAME=VALUE entries to the qemu:commandline environment of a libvirt domain definition.

usage: domain_env.py IN.xml OUT.xml NAME=VALUE [NAME=VALUE ...]
"""

import pathlib
import sys
import xml.etree.ElementTree as ET

QEMU_NS = "http://libvirt.org/schemas/domain/qemu/1.0"


def with_environment(xml, pairs):
    ET.register_namespace("qemu", QEMU_NS)
    root = ET.fromstring(xml)
    commandline = root.find(f"{{{QEMU_NS}}}commandline")
    if commandline is None:
        raise ValueError("the domain has no qemu:commandline")
    for pair in pairs:
        name, separator, value = pair.partition("=")
        if not name or not separator:
            raise ValueError(f"{pair!r} is not NAME=VALUE")
        for env in [env for env in commandline.findall(f"{{{QEMU_NS}}}env") if env.get("name") == name]:
            commandline.remove(env)
        ET.SubElement(commandline, f"{{{QEMU_NS}}}env", name=name, value=value)
    return ET.tostring(root, encoding="unicode")


def main(argv):
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    source, target, pairs = pathlib.Path(argv[1]), pathlib.Path(argv[2]), argv[3:]
    try:
        text = with_environment(source.read_text(), pairs)
    except (ValueError, ET.ParseError) as error:
        print(f"domain_env: {error}", file=sys.stderr)
        return 1
    target.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
