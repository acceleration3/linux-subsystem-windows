#!/usr/bin/env python3
"""The manifest of a render stack and the check of a running VM against it.

  stack_manifest.py make --stack DIR --driver-sha SHA [--driver-dirty] --suite-sha SHA
      writes DIR/manifest.json: the revisions the stack was built from and the md5 of the files that decide a result
  stack_manifest.py report [--domain NAME]
      on the host that runs the VM: prints a JSON object, basename -> md5, of the executor and the QEMU modules the
      VM's QEMU process has mapped (read from the mapping itself, so a file replaced after QEMU started is seen as it is)
  stack_manifest.py check --stack DIR --mapped FILE
      compares a report with DIR/manifest.json; exit 0 when every key file the VM runs is the stack's, 1 otherwise
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import time

KEY_FILES = ["libav_executor.so", "qemu-modules/hw-display-virtio-gpu-av.so", "qemu-modules/hw-display-virtio-gpu-pci-av.so"]
WATCHED = ("libav_executor.so", "hw-display-virtio-gpu-av.so", "hw-display-virtio-gpu-pci-av.so")


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def make_manifest(stack, driver_sha, driver_dirty, suite_sha, extra_files=()):
    stack = pathlib.Path(stack)
    files = {name: md5_of(stack / name) for name in list(KEY_FILES) + list(extra_files)}
    modules = (stack / "qemu-modules" / "hw-display-virtio-gpu-av.so").read_bytes()
    return {"driver_sha": driver_sha, "driver_dirty": driver_dirty, "suite_sha": suite_sha, "built": int(time.time()),
            "hybrid": b"offer_hybrid" in modules, "package_sha256": md5_of(stack / "av-drivers" / "package" / "package.sha256"),
            "files": files}


def mapped_regions(maps_text):
    regions = {}
    for line in maps_text.splitlines():
        parts = line.split(None, 5)
        if len(parts) < 6:
            continue
        path = parts[5].replace(" (deleted)", "")
        name = path.rsplit("/", 1)[-1]
        if name in WATCHED and name not in regions:
            regions[name] = (parts[0], path)
    return regions


def report(pid, maps_text=None, reader=md5_of):
    maps_text = maps_text if maps_text is not None else pathlib.Path(f"/proc/{pid}/maps").read_text()
    found = {}
    for name, (span, path) in mapped_regions(maps_text).items():
        try:
            found[name] = reader(f"/proc/{pid}/map_files/{span}")
        except OSError:
            found[name] = reader(path)
    return found


def check(manifest, mapped):
    problems = []
    for key, expected in manifest["files"].items():
        name = key.rsplit("/", 1)[-1]
        if name not in WATCHED:
            continue
        if name not in mapped:
            problems.append(f"{name} is not mapped by the VM's QEMU")
        elif mapped[name] != expected:
            problems.append(f"{name}: the VM runs md5 {mapped[name]}, the stack has {expected}")
    return problems


def qemu_pid(domain):
    out = subprocess.run(["pgrep", "-f", f"qemu-system-x86_64.*guest={domain}"], capture_output=True, text=True).stdout.split()
    if not out:
        raise SystemExit(f"no running QEMU for domain {domain}")
    return int(out[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="action", required=True)
    make = commands.add_parser("make")
    make.add_argument("--stack", required=True)
    make.add_argument("--driver-sha", required=True)
    make.add_argument("--driver-dirty", action="store_true")
    make.add_argument("--suite-sha", required=True)
    make.add_argument("--extra", action="append", default=[])
    rep = commands.add_parser("report")
    rep.add_argument("--domain", default="lsw")
    chk = commands.add_parser("check")
    chk.add_argument("--stack", required=True)
    chk.add_argument("--mapped", required=True)
    args = parser.parse_args()
    if args.action == "make":
        manifest = make_manifest(args.stack, args.driver_sha, args.driver_dirty, args.suite_sha, args.extra)
        (pathlib.Path(args.stack) / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
        return 0
    if args.action == "report":
        print(json.dumps(report(qemu_pid(args.domain))))
        return 0
    manifest = json.loads((pathlib.Path(args.stack) / "manifest.json").read_text())
    mapped = json.loads(pathlib.Path(args.mapped).read_text())
    problems = check(manifest, mapped)
    for problem in problems:
        print(f"stack check: {problem}", file=sys.stderr)
    print(f"stack check: {'MISMATCH' if problems else 'ok'} (driver {manifest['driver_sha'][:8]}, suite {manifest['suite_sha'][:8]})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
