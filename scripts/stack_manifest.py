#!/usr/bin/env python3
"""The manifest of a render stack and the check of a running VM against it.

  stack_manifest.py make --stack DIR --driver-sha SHA [--driver-dirty] --suite-sha SHA
      writes DIR/manifest.json: the revisions the stack was built from and the md5 of the files that decide a result
  stack_manifest.py report [--domain NAME]
      on the host that runs the VM: prints a JSON object, basename -> md5, of the executor and the QEMU modules the
      VM's QEMU process has mapped (read from the mapping itself, so a file replaced after QEMU started is seen as it is),
      and of the guest's C:\\av\\package\\package.sha256 under package.sha256 when the guest's share answers
  stack_manifest.py check --stack DIR --mapped FILE [--package]
      compares a report with DIR/manifest.json; exit 0 when every key file the VM runs is the stack's, and with
      --package the guest's driver package too, 1 otherwise
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

KEY_FILES = ["libav_executor.so", "qemu-modules/hw-display-virtio-gpu-av.so", "qemu-modules/hw-display-virtio-gpu-pci-av.so"]
WATCHED = ("libav_executor.so", "hw-display-virtio-gpu-av.so", "hw-display-virtio-gpu-pci-av.so")
GUEST_PACKAGE = "package.sha256"
SMB_PORT = 5445


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


def guest_package_md5(run=subprocess.run):
    with tempfile.TemporaryDirectory() as directory:
        local = pathlib.Path(directory) / GUEST_PACKAGE
        try:
            done = run(["smbclient", "//127.0.0.1/C$", "-p", str(SMB_PORT), "-U", "lsw", "-m", "SMB3", "-c",
                        f'get "\\av\\package\\{GUEST_PACKAGE}" "{local}"'], capture_output=True, text=True,
                       env=dict(os.environ, PASSWD="lsw"), timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            return None
        if done.returncode != 0 or "NT_STATUS" in done.stdout + done.stderr or not local.is_file():
            return None
        return md5_of(local)


def check(manifest, mapped, package=False):
    problems = []
    if package and mapped.get(GUEST_PACKAGE) is None:
        problems.append("the guest's driver package could not be read")
    elif package and mapped[GUEST_PACKAGE] != manifest["package_sha256"]:
        problems.append(f"the guest's driver package has md5 {mapped[GUEST_PACKAGE]}, the stack's has {manifest['package_sha256']}")
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
    chk.add_argument("--package", action="store_true")
    args = parser.parse_args()
    if args.action == "make":
        manifest = make_manifest(args.stack, args.driver_sha, args.driver_dirty, args.suite_sha, args.extra)
        (pathlib.Path(args.stack) / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
        return 0
    if args.action == "report":
        found = report(qemu_pid(args.domain))
        package = guest_package_md5()
        if package is not None:
            found[GUEST_PACKAGE] = package
        print(json.dumps(found))
        return 0
    manifest = json.loads((pathlib.Path(args.stack) / "manifest.json").read_text())
    mapped = json.loads(pathlib.Path(args.mapped).read_text())
    problems = check(manifest, mapped, args.package)
    for problem in problems:
        print(f"stack check: {problem}", file=sys.stderr)
    print(f"stack check: {'MISMATCH' if problems else 'ok'} (driver {manifest['driver_sha'][:8]}, suite {manifest['suite_sha'][:8]})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
