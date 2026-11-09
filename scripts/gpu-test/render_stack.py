#!/usr/bin/env python3
"""Puts the deployed accel-virt stack under the LSW VM of this host, and takes it out again.

Runs on the host under test, from the tree scripts/gpu-test/gpu-test.sh deploy-render shipped.

  render_stack.py install    save the VM's definition and its guest's driver package and agent, define the VM
                             with the tree's QEMU, executor and Direct3D runtime on every host GPU, install the
                             tree's driver package and agent in the guest, restart QEMU, and leave the VM running
  render_stack.py restore    put the saved definition, driver package and agent back
  render_stack.py status     what the VM and the guest run

The saved state is in ~/.local/share/lsw-gpu-test/stack-original and stays until restore finishes.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import time

import gpu_matrix as matrix
from lsw_link import AgentLink, GuestShare, LinkError, wait_for_agent

SAVED = matrix.STATE / "stack-original"
AGENT_GUEST = "C:\\ProgramData\\LSW\\lsw-agent.exe"
AGENT_STAGED = "C:\\av\\lsw-agent.new.exe"
AGENT_EXE = matrix.TREE / "build-agent" / "src" / "agent" / "lsw-guest-agent.exe"
SWAP_LOG = "C:\\av\\agent-swap.log"
SWAP_SCRIPT = "\r\n".join([
    "@echo off",
    f"echo swap started > {SWAP_LOG}",
    f"sc stop lsw-agent >> {SWAP_LOG} 2>&1",
    "ping -n 8 127.0.0.1 >nul",
    f"taskkill /F /IM lsw-agent.exe >> {SWAP_LOG} 2>&1",
    "ping -n 3 127.0.0.1 >nul",
    f"move /Y {AGENT_STAGED} {AGENT_GUEST} >> {SWAP_LOG} 2>&1",
    f"if exist {AGENT_STAGED} ping -n 6 127.0.0.1 >nul",
    f"if exist {AGENT_STAGED} move /Y {AGENT_STAGED} {AGENT_GUEST} >> {SWAP_LOG} 2>&1",
    f"sc start lsw-agent >> {SWAP_LOG} 2>&1",
    "",
])


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def swap_agent(link, share, local_exe):
    before = link.call("system_info").get("agent_version")
    share.put(str(local_exe), AGENT_STAGED)
    link.call("run_script", {"name": "render-stack agent swap", "script": SWAP_SCRIPT})
    time.sleep(10)
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        try:
            fresh = wait_for_agent(time.monotonic() + 60)
            after = fresh.call("system_info").get("agent_version")
            fresh.close()
            matrix.say(f"guest agent {before} -> {after}")
            return
        except LinkError:
            time.sleep(5)
    raise matrix.RunFailed("the guest agent did not come back after the swap")


def backup_guest(share, into):
    if into.exists():
        shutil.rmtree(into)
    into.mkdir(parents=True)
    code, out = share.smbclient(
        f'recurse ON; prompt OFF; lcd "{into}"; cd av; mget package; mget loaders; get provision.ps1; get adapters.txt', 600)
    share.get(AGENT_GUEST, into / "lsw-agent.exe")
    if not (into / "package" / "avumd.dll").exists() or not (into / "provision.ps1").exists():
        raise matrix.RunFailed(f"the guest's driver package could not be saved: {out.strip()[-300:]}")
    manifest = into / "package" / "package.sha256"
    if not manifest.exists():
        files = sorted(path for path in (into / "package").iterdir() if path.name != "package.sha256")
        manifest.write_text("".join(f"{sha(path)}  {path.name}\n" for path in files))


def restarted(domain, work, share):
    domain.stop()
    domain.start()
    return matrix.Guest(work, share).connect()


def command_install(args):
    domain = matrix.Domain()
    matrix.STATE.mkdir(parents=True, exist_ok=True)
    share = GuestShare()
    if SAVED.exists():
        matrix.say(f"{SAVED} exists: the stack is installed already, or an install did not finish; restore first")
        return 2
    required = [matrix.QEMU_BINARY, matrix.EXECUTOR, matrix.D3D_LIB, matrix.DRIVERS / "package" / "package.sha256",
                matrix.DRIVERS / "provision.ps1", AGENT_EXE]
    problems = [f"{path} is missing: deploy the tree again" for path in required if not path.exists()]
    if domain.exists() is False:
        problems.append(f"no libvirt domain '{matrix.DOMAIN}'")
    if subprocess.run(["pgrep", "-x", "lsw-manager"], capture_output=True).returncode == 0:
        problems.append("lsw-manager is running: close it, it redefines the VM")
    if problems:
        for problem in problems:
            matrix.say(f"preflight: {problem}")
        return 2
    was_running = domain.running()
    if not was_running:
        domain.start()
    link = wait_for_agent(time.monotonic() + matrix.BOOT_TIMEOUT)
    identity = {"domain_was_running": was_running, "agent_version": link.call("system_info").get("agent_version"),
                "status": link.call("gpu_status")}
    SAVED.mkdir(parents=True)
    (SAVED / "domain.xml").write_text(domain.definition())
    (SAVED / "identity.json").write_text(json.dumps(identity, indent=1))
    backup_guest(share, SAVED / "guest")
    matrix.say(f"saved the domain, the guest's driver package and its agent in {SAVED}")
    link.close()
    gpus = matrix.host_gpus()
    out = SAVED / "install-logs"
    out.mkdir(exist_ok=True)
    domain.stop()
    domain.define(matrix.retarget((SAVED / "domain.xml").read_text(), gpus, args.qemu_env))
    work = matrix.bridge_directory("stack")
    guest = matrix.boot(domain, work, share)
    try:
        if args.agent:
            swap_agent(guest.link, share, AGENT_EXE)
            guest.close()
            guest = matrix.Guest(work, share).connect()
        matrix.update_driver(guest, gpus, out, True)
    finally:
        guest.close()
    guest = restarted(domain, work, share)
    try:
        status = guest.link.call("gpu_status")
        version = guest.link.call("system_info").get("agent_version")
    finally:
        guest.close()
    (SAVED / "installed.json").write_text(json.dumps({"agent_version": version, "status": status}, indent=1))
    for adapter in status.get("adapters", []):
        matrix.say(f"guest adapter {adapter.get('name')}: {adapter.get('service')} {adapter.get('driver_version')} "
                   f"started={adapter.get('started')} problem={adapter.get('problem_code')}")
    unbound = [adapter.get("name") for adapter in status.get("adapters", []) if not matrix.bound(adapter)]
    if unbound:
        matrix.say(f"adapters not on avgpu: {unbound}")
        return 1
    matrix.say("the stack is installed and the VM is running it")
    return 0


def command_restore(args):
    if not SAVED.exists():
        matrix.say("nothing saved: the stack is not installed")
        return 0
    domain = matrix.Domain()
    share = GuestShare()
    identity = json.loads((SAVED / "identity.json").read_text())
    saved = SAVED / "guest"
    out = SAVED / "restore-logs"
    out.mkdir(exist_ok=True)
    work = matrix.bridge_directory("stack-restore")
    domain.stop()
    domain.define((SAVED / "domain.xml").read_text())
    domain.start()
    guest = matrix.Guest(work, share).connect()
    try:
        if (saved / "lsw-agent.exe").exists():
            swap_agent(guest.link, share, saved / "lsw-agent.exe")
            guest.close()
            guest = matrix.Guest(work, share).connect()
        matrix.update_driver(guest, matrix.host_gpus(), out, True, drivers=saved, own_loaders=False)
    finally:
        guest.close()
    guest = restarted(domain, work, share)
    try:
        theirs = share.get_bytes("C:\\av\\package\\package.sha256")
        ours = (saved / "package" / "package.sha256").read_bytes()
        matrix.say("guest driver package " + ("restored exactly" if theirs == ours else "differs from the saved one"))
        matrix.say(f"guest agent {guest.link.call('system_info').get('agent_version')} (was {identity['agent_version']})")
    finally:
        guest.close()
    if not identity.get("domain_was_running", True):
        domain.stop()
    shutil.rmtree(SAVED.with_name("stack-original.restored"), ignore_errors=True)
    shutil.move(str(SAVED), str(SAVED.with_name("stack-original.restored")))
    matrix.say("the VM's definition, driver package and agent are the saved ones again")
    return 0


def command_status(args):
    domain = matrix.Domain()
    print("saved state:", SAVED if SAVED.exists() else "none")
    print("domain:", domain.state())
    if domain.running():
        pids = subprocess.run(["pgrep", "-f", "qemu-system-x86_64.*guest=lsw"], capture_output=True, text=True).stdout.split()
        if pids:
            maps = pathlib.Path(f"/proc/{pids[0]}/maps").read_text()
            print("executor:", sorted({line.split()[-1] for line in maps.splitlines() if "libav_executor" in line}))
        link = AgentLink(timeout=20)
        print("guest agent:", link.call("system_info").get("agent_version"))
        for adapter in link.call("gpu_status").get("adapters", []):
            print("adapter:", adapter.get("name"), adapter.get("service"), adapter.get("driver_version"), adapter.get("started"))
        link.close()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    install = commands.add_parser("install")
    install.add_argument("--no-agent", dest="agent", action="store_false")
    install.add_argument("--qemu-env", action="append", default=[], metavar="NAME=VALUE")
    commands.add_parser("restore")
    commands.add_parser("status")
    args = parser.parse_args()
    handler = {"install": command_install, "restore": command_restore, "status": command_status}[args.command]
    try:
        return handler(args)
    except (matrix.RunFailed, LinkError, matrix.AgentError, OSError) as error:
        matrix.say(f"{args.command} failed: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
