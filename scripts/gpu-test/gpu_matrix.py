#!/usr/bin/env python3
"""Every graphics API's tests on every GPU of this host, in its LSW VM.

Runs on the host under test, from the tree scripts/gpu-test/gpu-test.sh
deployed (or from a checkout). For each host GPU the VM is redefined with one
accel-virt adapter, bound to that GPU and driving the display, so Direct3D,
OpenGL, Vulkan and OpenCL in the guest all reach it by default; then:

  probes   avd3d9probe, avd3d11probe, avd3d12probe (the system runtimes, so
           D3D9 and D3D11 through avumd.dll), avvkprobe, avglprobe and
           avclprobe, in the console session, judged step by step as
           tools/guest/run_guest_suite.py judges them
  d3d11 d3d12 vulkan opengl opencl
           accel-virt's sample suites (tools/<api>_report.py): every sample in
           the VM and natively on the same host GPU, judged by whether the VM
           ran it and whether its picture matches the host's
  clgl     the OpenCL-OpenGL sharing samples (tools/clgl_samples.py), each
           judging the values it moved itself
  traces   (opt-in) API traces recorded from real applications, replayed and
           judged on their pictures, buffers, memory and speed (trace_suite.py,
           TRACES.md); --traces picks some

The guest is driven through qga_bridge.py (LSW's agent and the C$ share as
lsw/lsw), so accel-virt's tools run unchanged. Before the first GPU the VM
boots once with every host GPU attached and the deployed driver package is
installed, if the guest's differs: the package's INF names only the adapters
present when it is signed.

  gpu_matrix.py preflight
  gpu_matrix.py gpus
  gpu_matrix.py run --out DIR [--gpus 0,2] [--suites probes,vulkan] [--frames N]
  gpu_matrix.py restore           # put back the domain a killed run left changed

The VM's own definition is saved first and put back at the end, running again
if it was running. lsw-manager must not be running (it would redefine the
domain under the run); --allow-manager overrides the check.
"""
import argparse
import datetime
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
import xml.etree.ElementTree as ET

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[1]
AV = TREE / "external" / "accel-virt"
sys.path[:0] = [str(HERE), str(AV / "tools"), str(AV / "tools" / "guest"), str(AV / "tools" / "host")]

import list_gpus  # noqa: E402
import run_guest_suite as suite  # noqa: E402
from guest_agent import AgentError, wait_for_agent as wait_for_bridge  # noqa: E402
from lsw_link import GuestShare, LinkError, wait_for_agent  # noqa: E402
from qga_bridge import Bridge  # noqa: E402
from app_suites import APP_SUITES, Desktop, run_app_suite  # noqa: E402
from trace_suite import TRACE_SUITES, run_trace_suite  # noqa: E402

DOMAIN = "lsw"
URI = "qemu:///session"
QEMU_NS = "http://libvirt.org/schemas/domain/qemu/1.0"
STATE = pathlib.Path(os.environ.get("XDG_DATA_HOME", pathlib.Path.home() / ".local" / "share")) / "lsw-gpu-test"
SAVED_DOMAIN = STATE / "original-domain.xml"

QEMU_BINARY = TREE / "build" / "accel-virt" / "qemu" / "qemu" / "qemu-system-x86_64"
QEMU_MODULES = QEMU_BINARY.parent
# Libraries QEMU links that this host lacks, shipped by gpu-test.sh deploy.
SHIPPED_LIBRARIES = TREE / "libs"
LOSS_LAYER = AV / "build" / "gpu_fault" / "libav_device_loss_layer.so"
EXECUTOR = TREE / "build" / "accel-virt" / "qemu" / "libav_executor.so"
D3D_LIB = TREE / "build" / "accel-virt" / "d3d" / "lib"
# Where the host drivers keep QEMU's compiled shaders across boots: the
# same directory and variables lsw-manager's domain uses.
SHADER_CACHE = pathlib.Path.home() / ".cache" / "lsw" / "shader-cache"
SHADER_CACHE_ENV = ("MESA_SHADER_CACHE_DIR", "__GL_SHADER_DISK_CACHE_PATH", "DXVK_STATE_CACHE_PATH",
                    "VKD3D_SHADER_CACHE_PATH")
DRIVERS = TREE / "build" / "av-drivers"
VULKAN_LOADERS = {"x64": TREE / "build-agent" / "_deps" / "vulkanloader-build" / "loader" / "vulkan-1.dll",
                  "x86": TREE / "build-agent32" / "_deps" / "vulkanloader-build" / "loader" / "vulkan-1.dll"}
GUEST_PROGRAMS = AV / "build" / "guest"
HOST_PROGRAMS = [AV / "build" / name for name in
                 ("avd3d11samples_host", "avd3d12samples_host", "avglsamples_host", "avclsamples_host")] + \
                [AV / "build-vulkan-samples" / "host" / "avvksamples"]

GUEST_DIR = "C:\\av\\gputest"
GPU_DEVICE = "virtio-gpu-av-pci"
FIRST_RENDER_SLOT = 0x12
SUITES = ("probes", "d3d11", "d3d12", "vulkan", "opengl", "opencl", "clgl")
PROBES = {
    "d3d9": ("avd3d9probe", suite.D3D_STEPS["9"]),
    "d3d11": ("avd3d11probe", suite.D3D_STEPS["11"]),
    "d3d12": ("avd3d12probe", suite.D3D_STEPS["12"]),
    "vulkan": ("avvkprobe", suite.PROBE_STEPS),
    "opengl": ("avglprobe", suite.GL_STEPS),
    "opencl": ("avclprobe", suite.CL_STEPS),
}
SUMMARY = re.compile(r"^(av\w+probe): (\d+) passed, (\d+) failed\r?$", re.M)
STEP = re.compile(r"^(ok|fail|FAIL|failed) (\w+)\.(\w+)(?: (.*?))?\r?$", re.M)
NOTE = re.compile(r"^note (\w+)\.(\w+) (.*?)\r?$", re.M)
BOOT_TIMEOUT = 35 * 60       # the agent listens only once the desktop is usable, or after 30 minutes
SHUTDOWN_TIMEOUT = 180
REPORT_TIMEOUT = 90 * 60
INSTALL_TIMEOUT = 20 * 60
DRIVER_RESTART = 3010


def say(line):
    print(f"[{datetime.datetime.now():%H:%M:%S}] {line}", flush=True)


class RunFailed(Exception):
    """A step the rest of a GPU's pass cannot go on without."""


# --------------------------------------------------------------------- the VM

class Domain:
    """The LSW VM's libvirt domain in the user's session."""

    def virsh(self, *arguments, check=True, timeout=300):
        done = subprocess.run(["virsh", "-c", URI, *arguments], capture_output=True, text=True, timeout=timeout,
                              env=dict(os.environ, LC_ALL="C"))
        if check and done.returncode != 0:
            raise RunFailed(f"virsh {' '.join(arguments)}: {(done.stdout + done.stderr).strip()}")
        return done.stdout

    def exists(self):
        return subprocess.run(["virsh", "-c", URI, "dominfo", DOMAIN], capture_output=True).returncode == 0

    def state(self):
        return self.virsh("domstate", DOMAIN).strip()

    def running(self):
        return self.state() == "running"

    def shut_off(self):
        # "in shutdown", "paused" and "crashed" are still active: start refuses them.
        return self.state() == "shut off"

    def definition(self):
        return self.virsh("dumpxml", "--inactive", DOMAIN)

    def define(self, xml):
        with tempfile.NamedTemporaryFile("w", suffix=".xml", delete=False) as staged:
            staged.write(xml)
        try:
            self.virsh("define", staged.name)
        finally:
            os.unlink(staged.name)

    def start(self):
        self.virsh("start", DOMAIN)

    def stop(self):
        if self.shut_off():
            return
        self.virsh("shutdown", DOMAIN, check=False)
        deadline = time.monotonic() + SHUTDOWN_TIMEOUT
        while time.monotonic() < deadline:
            if self.shut_off():
                return
            time.sleep(3)
        say("the guest did not shut down in time: destroying it")
        self.virsh("destroy", DOMAIN, check=False)
        deadline = time.monotonic() + 60
        while not self.shut_off():
            if time.monotonic() > deadline:
                raise RunFailed(f"{DOMAIN} is still {self.state()} after virsh destroy")
            time.sleep(1)


class DeviceProperties:
    """A ``-device`` value: the driver and its properties, in order."""

    def __init__(self, value):
        driver, *pairs = value.split(",")
        self.driver = driver
        self.pairs = [tuple(pair.split("=", 1)) if "=" in pair else (pair, None) for pair in pairs]

    def get(self, key):
        return next((value for name, value in self.pairs if name == key), None)

    def set(self, key, value):
        for index, (name, _) in enumerate(self.pairs):
            if name == key:
                self.pairs[index] = (key, value)
                return
        self.pairs.append((key, value))

    def drop(self, key):
        self.pairs = [(name, value) for name, value in self.pairs if name != key]

    def text(self):
        return ",".join([self.driver] + [name if value is None else f"{name}={value}" for name, value in self.pairs])


def emulator_path():
    """The QEMU the domain names. With libraries shipped to this host, a
    wrapper that puts them on the library path: libvirt also starts the
    emulator to probe its capabilities, outside the domain's qemu:env."""
    if not SHIPPED_LIBRARIES.is_dir():
        return QEMU_BINARY
    wrapper = QEMU_BINARY.with_name(QEMU_BINARY.name + "-shipped-libs")
    wrapper.write_text(f'#!/bin/sh\nLD_LIBRARY_PATH="{SHIPPED_LIBRARIES}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}" '
                       f'exec "{QEMU_BINARY}" "$@"\n')
    wrapper.chmod(0o755)
    return wrapper


def retarget(xml, gpus, qemu_env=()):
    """The domain, running this tree's QEMU and executor, with one accel-virt
    adapter per GPU in `gpus`: the first drives the display, the rest render.
    `qemu_env` is NAME=VALUE pairs set in QEMU's environment, for an
    experiment such as a host driver's debug flag."""
    ET.register_namespace("qemu", QEMU_NS)
    root = ET.fromstring(xml)
    emulator = root.find("./devices/emulator")
    if emulator is None:
        raise RunFailed("the domain names no emulator")
    emulator.text = str(emulator_path())
    # A run started over ssh may have no PipeWire to play into, and QEMU does
    # not start when its audio backend fails.
    for audio in root.findall("./devices/audio"):
        for child in list(audio):
            audio.remove(child)
        audio.attrib = {"id": audio.get("id", "1"), "type": "none"}
    commandline = root.find(f"{{{QEMU_NS}}}commandline")
    if commandline is None:
        raise RunFailed("the domain has no qemu:commandline: it is not an accel-virt VM")
    environment = commandline.findall(f"{{{QEMU_NS}}}env")
    if SHIPPED_LIBRARIES.is_dir() and not any(env.get("name") == "LD_LIBRARY_PATH" for env in environment):
        added_env = ET.SubElement(commandline, f"{{{QEMU_NS}}}env", name="LD_LIBRARY_PATH")
        environment.append(added_env)
    for env in environment:
        if env.get("name") == "LD_LIBRARY_PATH":
            env.set("value", str(SHIPPED_LIBRARIES))
        elif env.get("name") == "QEMU_MODULE_DIR":
            env.set("value", str(QEMU_MODULES))
        elif env.get("name") == "AV_D3D_PATH":
            env.set("value", str(D3D_LIB))
    # The host drivers' shader caches kept across boots, as lsw-manager's
    # domain keeps them: libvirt's per-domain XDG_CACHE_HOME is deleted at
    # shutdown, and a cold cache made RE4's trace take 285 s where a warm
    # one took 29 s on the NVIDIA host.
    SHADER_CACHE.mkdir(parents=True, exist_ok=True)
    for name in SHADER_CACHE_ENV:
        for env in [env for env in environment if env.get("name") == name]:
            commandline.remove(env)
        ET.SubElement(commandline, f"{{{QEMU_NS}}}env", name=name, value=str(SHADER_CACHE))
    for pair in qemu_env:
        name, _, value = pair.partition("=")
        for env in [env for env in environment if env.get("name") == name]:
            commandline.remove(env)
        ET.SubElement(commandline, f"{{{QEMU_NS}}}env", name=name, value=value)
    arguments = commandline.findall(f"{{{QEMU_NS}}}arg")
    template = None
    for index, argument in enumerate(arguments):
        value = argument.get("value", "")
        drop_pair = value.startswith(GPU_DEVICE + ",") or (value == "-L" and index + 1 < len(arguments))
        if value.startswith(GPU_DEVICE + ",") and template is None:
            template = DeviceProperties(value)
        if drop_pair:
            commandline.remove(argument)
            commandline.remove(arguments[index + 1] if value == "-L" else arguments[index - 1])
    if template is None:
        raise RunFailed(f"the domain has no {GPU_DEVICE}")
    template.drop("display")
    template.set("executor", str(EXECUTOR))
    added = []
    for position, gpu in enumerate(gpus):
        device = DeviceProperties(template.text())
        device.set("host-gpu", gpu.uuid)
        if position > 0:
            device.set("id", f"{template.get('id') or 'lsw-gpu'}-{position}")
            device.set("addr", f"0x{FIRST_RENDER_SLOT + position - 1:x}")
            device.set("display", "off")
            device.drop("max_outputs")
            device.drop("export-socket")
        added += [ET.Element(f"{{{QEMU_NS}}}arg", value="-device"),
                  ET.Element(f"{{{QEMU_NS}}}arg", value=device.text())]
    indent = commandline.text or "\n    "
    for offset, element in enumerate(added):
        element.tail = indent
        commandline.insert(offset, element)
    return ET.tostring(root, encoding="unicode")


def avgpu_log_path(xml):
    found = re.search(r"file,id=avgpu-log,path=([^,'\"]+)", xml)
    return pathlib.Path(found.group(1)) if found else None


# --------------------------------------------------------------- the host GPUs

def host_gpus():
    text = subprocess.run(["vulkaninfo"], capture_output=True, text=True, check=True).stdout
    return [gpu for gpu in list_gpus.parse(text) if gpu.kind != "cpu"]


def short_name(gpu):
    """The Vulkan name less the driver's parenthesised suffix: what a GL
    renderer string or an OpenCL device name starts the same way."""
    return re.sub(r"\s*\([^()]*\)\s*$", "", gpu.name).strip()


def slug(gpu):
    return re.sub(r"[^a-z0-9]+", "-", f"{gpu.index}-{short_name(gpu)}".lower()).strip("-")[:48].rstrip("-")


def bridge_directory(name):
    """Where a pass's agent.sock goes: a unix socket's path is at most 107
    bytes, which a run directory under the home easily passes."""
    directory = pathlib.Path(f"/tmp/lsw-gpu-test-{os.getuid()}") / str(name)
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def normalized(text):
    return re.sub(r"[^a-z0-9]+", " ", re.sub(r"\((r|tm)\)", "", text.lower())).strip()


def opencl_device_index(gpu):
    """The host OpenCL device avclsamples_host --device should take for `gpu`:
    GPU devices only, numbered across platforms in the loader's order, as the
    samples count them (samples/opencl/framework.cpp) - a CPU device listed
    first would put every index one off. None when no GPU's name matches."""
    try:
        raw = subprocess.run(["clinfo", "--raw"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    names = []
    name = None
    for line in raw.splitlines():
        found = re.match(r"^\[[^]]+\]\s+(CL_DEVICE_NAME|CL_DEVICE_TYPE)\s+(.*?)\s*$", line)
        if found is None:
            continue
        if found.group(1) == "CL_DEVICE_NAME":
            name = found.group(2)
        elif name is not None:
            if "CL_DEVICE_TYPE_GPU" in found.group(2):
                names.append(name)
            name = None
    wanted = normalized(short_name(gpu))
    for index, candidate in enumerate(names):
        have = normalized(candidate)
        if wanted and (wanted in have or have in wanted):
            return index
    return None


def host_environment(gpu):
    """What makes the host halves of the sample suites run on `gpu` too."""
    environment = dict(os.environ)
    environment.update({
        "DXVK_FILTER_DEVICE_NAME": short_name(gpu),
        "VKD3D_FILTER_DEVICE_NAME": short_name(gpu),
        "MESA_VK_DEVICE_SELECT": f"{gpu.vendor:04x}:{gpu.device:04x}",
        "MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE": "1",
        "AV_GL_GPU": short_name(gpu),
    })
    # DXVK's native WSI starts SDL's video subsystem even to render off
    # screen. A run started over ssh has no display for it, and one that has
    # a desktop should not draw on it; SDL's offscreen driver is the one
    # that still offers Vulkan.
    environment["SDL_VIDEO_DRIVER"] = "offscreen"
    return environment


# ------------------------------------------------------------------ preflight

def preflight(allow_manager=False):
    problems = []
    for tool in ("virsh", "smbclient", "vulkaninfo", "clinfo"):
        if shutil.which(tool) is None:
            problems.append(f"{tool} is not installed")
    if not os.access("/dev/kvm", os.R_OK | os.W_OK):
        problems.append("/dev/kvm is not usable by this user")
    domain = Domain()
    if shutil.which("virsh") and not domain.exists():
        problems.append(f"no libvirt domain '{DOMAIN}' in {URI}: install the LSW VM first (gpu-test.sh provision)")
    if not allow_manager and subprocess.run(["pgrep", "-x", "lsw-manager"], capture_output=True).returncode == 0:
        problems.append("lsw-manager is running: close it (it redefines the VM), or pass --allow-manager")
    required = [QEMU_BINARY, EXECUTOR, D3D_LIB, DRIVERS / "package" / "package.sha256", DRIVERS / "provision.ps1",
                *HOST_PROGRAMS, *(GUEST_PROGRAMS / f"{name}.exe" for name, _ in PROBES.values()),
                AV / "build-vulkan-samples" / "guest" / "avvksamples.exe",
                AV / "third_party" / "vulkan-runtime" / "vulkan-1.dll",
                AV / "third_party" / "dxc" / "dxcompiler.dll",
                AV / "third_party" / "vcredist" / "vc_redist.x64.exe",
                AV / "third_party" / "vulkan-samples" / "Vulkan-Samples" / "data"]
    problems += [f"{path} is missing: deploy the tree again" for path in required if not path.exists()]
    for binary in [QEMU_BINARY, EXECUTOR, *HOST_PROGRAMS]:
        if not binary.exists():
            continue
        linked = subprocess.run(["ldd", str(binary)], capture_output=True, text=True,
                                env=dict(os.environ, LD_LIBRARY_PATH=str(SHIPPED_LIBRARIES))).stdout
        missing = sorted(set(re.findall(r"^\s*(\S+) => not found", linked, re.M)))
        if missing:
            problems.append(f"{binary.name} needs {', '.join(missing)}, which this host does not have")
    try:
        gpus = host_gpus()
        if not gpus:
            problems.append("vulkaninfo lists no GPU")
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        problems.append(f"vulkaninfo failed: {error}")
    return problems


# ------------------------------------------------------------------- the guest

class Guest:
    """One boot of the VM: the agent link, the share and the bridge."""

    def __init__(self, work, share):
        self.work = work
        self.share = share
        self.link = None
        self.bridge = None
        self.agent = None

    def connect(self):
        self.link = wait_for_agent(time.monotonic() + BOOT_TIMEOUT)
        self.bridge = Bridge(self.work, self.link, self.share).start()
        self.agent = wait_for_bridge(str(self.work / "agent.sock"), time.monotonic() + 60, 600.0)
        return self

    def close(self):
        if self.agent is not None:
            self.agent.close()
        if self.bridge is not None:
            self.bridge.stop()
        if self.link is not None:
            self.link.close()
        self.agent = self.bridge = self.link = None

    def run(self, path, arguments, timeout=600):
        code, out, error = self.agent.run(path, arguments, timeout)
        return code, out.decode(errors="replace") + error.decode(errors="replace")

    def powershell(self, script, timeout=600):
        return self.run(suite.POWERSHELL, ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script], timeout)


def boot(domain, work, share):
    say("booting the guest")
    domain.start()
    guest = Guest(work, share).connect()
    say("the guest agent answers")
    return guest


def driver_files():
    """(local file, guest path) for the package update, as GuestDriverUpdate::files_of lays them out."""
    files = [(path, f"C:\\av\\incoming\\package\\{path.name}")
             for path in sorted((DRIVERS / "package").iterdir()) if path.is_file()]
    files.append((DRIVERS / "provision.ps1", "C:\\av\\provision.ps1"))
    for arch in ("x64", "x86"):
        loaders = DRIVERS / "loaders" / arch
        for path in sorted(loaders.iterdir()) if loaders.is_dir() else []:
            if path.name.lower() != "vulkan-1.dll" or not VULKAN_LOADERS[arch].exists():
                files.append((path, f"C:\\av\\loaders\\{arch}\\{path.name}"))
        if VULKAN_LOADERS[arch].exists():
            files.append((VULKAN_LOADERS[arch], f"C:\\av\\loaders\\{arch}\\vulkan-1.dll"))
    return files


def install_script(staged):
    """The manager's driver update (GuestDriverUpdate::staging, then
    GuestDriverInstaller::script) as one batch file: `staged` maps a file
    pushed under C:\\av\\gputest-stage to where it belongs; empty, the package
    the guest already keeps is signed again for the adapters present now."""
    lines = ["@echo off", "setlocal"]
    if staged:
        lines += ["if exist C:\\av\\incoming rmdir /s /q C:\\av\\incoming",
                  "mkdir C:\\av\\incoming\\package || exit /b 4"]
        for source, target in staged:
            directory = target.rsplit("\\", 1)[0]
            lines.append(f'if not exist "{directory}" mkdir "{directory}"')
            if target.startswith("C:\\av\\loaders\\"):
                arch = target.split("\\")[3]
                system = "SysWOW64" if arch == "x86" else "System32"
                installed = f"C:\\Windows\\{system}\\{target.rsplit(chr(92), 1)[1]}"
                lines += ["set OURS=0", f'fc /b "{installed}" "{target}" >nul 2>&1 && set OURS=1',
                          f'copy /y "{source}" "{target}" >nul || exit /b 4',
                          f'if "%OURS%"=="1" copy /y "{target}" "{installed}" >nul']
            else:
                lines.append(f'copy /y "{source}" "{target}" >nul || exit /b 4')
        lines.append("set SOURCE=C:\\av\\incoming")
    lines += [
        "if not defined SOURCE set SOURCE=C:\\av\\pristine",
        "if not exist %SOURCE%\\package (echo %SOURCE% has no package & exit /b 2)",
        "robocopy %SOURCE% C:\\av\\resign /MIR /NFL /NDL /NJH /NJS /NP >nul",
        "if %ERRORLEVEL% GEQ 8 exit /b %ERRORLEVEL%",
        "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\av\\provision.ps1 -Package C:\\av\\resign\\package -Stage sign",
        "set SIGNED=%ERRORLEVEL%",
        'if not "%SIGNED%"=="0" if not "%SIGNED%"=="3010" exit /b %SIGNED%',
        "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\av\\provision.ps1 -Package C:\\av\\resign\\package -Stage install -NoVerifier",
        "set INSTALLED=%ERRORLEVEL%",
        'if not "%INSTALLED%"=="0" if not "%INSTALLED%"=="3010" exit /b %INSTALLED%',
        'if /i not "%SOURCE%"=="C:\\av\\pristine" (',
        "  robocopy %SOURCE%\\package C:\\av\\pristine\\package /MIR /NFL /NDL /NJH /NJS /NP >nul",
        "  rmdir /s /q %SOURCE%",
        ")",
        'if "%INSTALLED%"=="0" if "%SIGNED%"=="3010" exit /b 3010',
        "exit /b %INSTALLED%",
    ]
    return "\r\n".join(lines) + "\r\n"


def run_install(guest, staged, log):
    script = "C:\\av\\gputest-install.cmd"
    guest.share.put_bytes(install_script(staged).encode(), script)
    code, output = guest.run("C:\\Windows\\System32\\cmd.exe", ["/c", script], INSTALL_TIMEOUT)
    log.write_text(output)
    if code not in (0, DRIVER_RESTART):
        raise RunFailed(f"the driver install exited {code}; see {log}")
    return code


def update_driver(guest, gpus, out, force):
    """The deployed package installed in the guest, if the guest's differs."""
    ours = (DRIVERS / "package" / "package.sha256").read_bytes()
    try:
        theirs = guest.share.get_bytes("C:\\av\\pristine\\package\\package.sha256")
    except LinkError:
        theirs = b""
    guest.share.put_bytes(list_gpus.adapter_names(gpus).encode(), "C:\\av\\adapters.txt")
    if theirs == ours and not force:
        say("the guest's driver package is the deployed one; signing it for every GPU again")
        run_install(guest, [], out / "driver-resign.log")
        return
    say("installing the deployed driver package")
    stage = "C:\\av\\gputest-stage"
    staged = []
    guest.share.make_directories(stage)
    for index, (local, target) in enumerate(driver_files()):
        source = f"{stage}\\{index:02d}-{local.name}"
        guest.share.put(local, source)
        staged.append((source, target))
    run_install(guest, staged, out / "driver-install.log")


def adapter_state(guest, gpu):
    """gpu_status's entry for the adapter bound to `gpu`, and the whole reply."""
    status = guest.link.call("gpu_status")
    vendor, device, _ = gpu.pci_identity()
    for adapter in status.get("adapters", []):
        if pci_number(adapter.get("vendor_id")) == vendor and pci_number(adapter.get("device_id")) == device:
            return adapter, status
    return None, status


def pci_number(value):
    """A PCI ID as gpu_status gives it: a number, or hex text."""
    if isinstance(value, int):
        return value
    try:
        return int(str(value), 16)
    except ValueError:
        return None


def bound(adapter):
    return (adapter is not None and str(adapter.get("service", "")).lower() == "avgpu"
            and adapter.get("started") and not adapter.get("problem_code"))


# ------------------------------------------------------------------ the suites

LOSS_STEPS = ("loss_lose", "loss_removed", "loss_reason", "loss_recreated", "loss_texel")
"""What the D3D11 probe must pass when the host was started with the
device-loss layer: its device lost, the removal told, a new one drawing."""
LOSS_RECOVERY_SECONDS = 30
"""How long the desktop may stay dark after the probe lost its device."""


LOSS_LIT_KEPT = 0.9
"""What share of what the desktop lit before the loss it must light again."""


def desktop_recovered(desktop, before):
    """Whether the desktop is drawn again within LOSS_RECOVERY_SECONDS of the
    probe's device loss: as much of it lit as before the loss, whatever the
    wallpaper, and the taskbar - explorer's own device - as bright."""
    deadline = time.monotonic() + LOSS_RECOVERY_SECONDS
    while True:
        shown = desktop.screenshot("after-loss")
        if (shown.get("lit", 0.0) >= LOSS_LIT_KEPT * before.get("lit", 0.0) > 0.0
                and shown.get("taskbar", 0.0) >= LOSS_LIT_KEPT * before.get("taskbar", 0.0)):
            return True, shown
        if time.monotonic() > deadline:
            return False, shown
        time.sleep(3)


def run_probes(guest, out, device_loss=False):
    """Each probe in the console session, judged on its summary and on every
    step its API must report; with the device-loss layer, the D3D11 probe's
    loss steps too, and the desktop composed again after them."""
    results = {}
    out.mkdir(parents=True, exist_ok=True)
    console = suite.ConsoleProgram(guest.agent, 900)
    guest.powershell(f"New-Item -ItemType Directory -Force {GUEST_DIR} | Out-Null")
    for api, (program, expected) in PROBES.items():
        guest.agent.push((GUEST_PROGRAMS / f"{program}.exe").read_bytes(), f"{GUEST_DIR}\\{program}.exe")
    desktop = Desktop(guest, out, "")
    if device_loss:
        desktop.prepare()
    for api, (program, expected) in PROBES.items():
        say(f"probe {program}")
        before = desktop.screenshot("before-loss") if api == "d3d11" and device_loss else {}
        report = f"{GUEST_DIR}\\{program}.txt"
        local = out / f"{program}.txt"
        failures = []
        try:
            code = console.run(f"cd /d {GUEST_DIR} && {GUEST_DIR}\\{program}.exe", report, program)
            guest.agent.pull(report, local)
            text = local.read_text(errors="replace")
        except (AgentError, suite.StepFailed, OSError) as error:
            results[api] = {"status": "error", "failures": [{"case": program, "detail": str(error)}]}
            continue
        summary = next((found for found in SUMMARY.finditer(text) if found.group(1) == program), None)
        reported = {step for _, _, step, _ in STEP.findall(text)}
        passed = {step for verdict, _, step, _ in STEP.findall(text) if verdict == "ok"}
        if summary is None:
            failures.append({"case": program, "detail": f"no summary line (exit {code})"})
        elif int(summary.group(3)) or code:
            failures.append({"case": program, "detail": f"{summary.group(3)} step(s) failed, exit {code}"})
        for verdict, _, step, detail in STEP.findall(text):
            if verdict != "ok":
                failures.append({"case": f"{program}.{step}", "detail": detail or verdict})
        if api == "d3d11" and device_loss:
            expected = tuple(expected) + LOSS_STEPS
            recovered, shown = desktop_recovered(desktop, before)
            if not recovered:
                failures.append({"case": f"{program}.desktop", "detail": "the desktop is not drawn again "
                                 f"{LOSS_RECOVERY_SECONDS} s after the device loss ({shown.get('lit', 0.0):.1%} "
                                 f"lit, {before.get('lit', 0.0):.1%} before; taskbar {shown.get('taskbar', 0.0):.0f}, "
                                 f"{before.get('taskbar', 0.0):.0f} before)"})
        for step in expected:
            if step not in passed and step not in reported:
                failures.append({"case": f"{program}.{step}", "detail": "never reported"})
        notes = {f"{scope}.{key}": value for scope, key, value in NOTE.findall(text)}
        renderer = notes.get("opengl.renderer", "")
        if api == "opengl" and any(soft in renderer.lower() for soft in suite.SOFTWARE_RENDERERS):
            failures.append({"case": f"{program}.renderer", "detail": f"a software renderer: {renderer}"})
        results[api] = {"status": "fail" if failures else "pass", "failures": failures, "log": str(local),
                        "notes": notes}
    # The probes' pictures, which they write to C:\av whatever their own directory.
    for name in [picture + ".bmp" for picture in
                 suite.VULKAN_PICTURES + suite.GL_PICTURES + suite.D3D_PICTURES]:
        try:
            guest.agent.pull(f"{suite.GUEST_DIRECTORY}\\{name}", out / name)
        except (AgentError, OSError):
            (out / name).unlink(missing_ok=True)
    return results


def console_user(guest, fallback):
    """The console session's user as COMPUTER\\user: a bare name can be the
    computer's own (LSW's VM is named lsw too), and a scheduled task's
    principal then fails to register."""
    if "\\" in fallback:
        return fallback
    code, output = guest.powershell("(Get-CimInstance Win32_ComputerSystem).UserName", 120)
    user = output.strip().splitlines()[-1].strip() if output.strip() else ""
    return user if code == 0 and "\\" in user else fallback


def report_command(api, work, out, args, gpu, user):
    command = [sys.executable, "-B", str(AV / "tools" / f"{api}_report.py"), "--work", str(work), "--no-build",
               "-o", str(out), "--guest-user", user]
    if api == "opencl":
        command += ["--frames", str(max(1, args.frames // 6)), "--warmup", "2"]
        device = opencl_device_index(gpu)
        if device is not None:
            command += ["--device", str(device)]
    else:
        command += ["--frames", str(args.frames), "--warmup", str(args.warmup)]
    if api == "opengl":
        command += ["--gpu", short_name(gpu)]
    if api in ("d3d11", "d3d12", "vulkan"):
        command += ["--gpu-name", gpu.name]
    return command


# Samples whose native run cannot work on a GPU, each with why: (suite, sample,
# a substring of the GPU's name, the reason). Anything else the host half did
# not run to a picture fails the suite, since the VM's picture then has nothing
# to be compared with. Empty: every sample runs natively on every GPU tested.
HOST_EXCEPTIONS = ()


def host_exception(api, name, gpu_name):
    """Why `name` legitimately has no native picture on this GPU, or None."""
    return next((why for suite_name, sample, gpu_part, why in HOST_EXCEPTIONS
                 if suite_name == api and sample == name and gpu_part in gpu_name), None)


def judge_summary(path, api, gpu_name):
    """A sample suite's failures from its summary.json: a sample either half
    did not run, or whose picture is unlike the host's. Only a sample in
    HOST_EXCEPTIONS may lack its host half, and it is recorded with the
    reason, not passed silently."""
    summary = json.loads(path.read_text())
    failures, notes = [], []
    for sample in summary.get("samples", []):
        name, vm, host = sample.get("name"), sample.get("vm_status"), sample.get("host_status")
        if host != "ok":
            why = host_exception(api, name, gpu_name)
            entry = {"case": name, "detail": f"host: {host or 'did not run'}; VM: {vm or 'did not run'}"}
            if why is None:
                failures.append(entry)
            else:
                notes.append({"case": name, "detail": f"{entry['detail']}; excepted: {why}"})
        elif vm != "ok":
            failures.append({"case": name, "detail": f"VM: {vm or 'did not run'}; host: ok"})
        elif sample.get("tone") == "bad":
            failures.append({"case": name, "detail": f"picture {sample.get('picture')} (host: {host})"})
        elif sample.get("tone") == "warn":
            notes.append({"case": name, "detail": f"picture {sample.get('picture')}"})
    return failures, notes, len(summary.get("samples", []))


def run_clgl(work, out, user):
    """The OpenCL-OpenGL sharing samples: no host half, each sample checks
    what it moved and prints "ok <name>" or "FAIL <name>: <why>"."""
    say("suite clgl")
    out.mkdir(parents=True, exist_ok=True)
    log = out / "run.log"
    try:
        done = subprocess.run([sys.executable, "-B", str(AV / "tools" / "clgl_samples.py"), "--work", str(work),
                               "--guest-user", user], capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        return {"status": "error", "failures": [{"case": "clgl", "detail": "ran past 15 min"}], "log": str(log)}
    text = done.stdout + done.stderr
    log.write_text(text)
    passed = re.findall(r"^ok (\S+)\r?$", text, re.M)
    failures = [{"case": name, "detail": why.strip()}
                for name, why in re.findall(r"^FAIL ([^:\r\n]+):?(.*?)\r?$", text, re.M)]
    if not passed and not failures:
        failures.append({"case": "clgl", "detail": f"no sample reported (exit {done.returncode})"})
    return {"status": "fail" if failures else "pass", "failures": failures, "samples": len(passed) + len(failures),
            "log": str(log)}


def run_report(api, work, out, args, gpu, user):
    say(f"suite {api}")
    out.mkdir(parents=True, exist_ok=True)
    log = out / "run.log"
    try:
        with open(log, "w") as written:
            done = subprocess.run(report_command(api, work, out, args, gpu, user), stdout=written,
                                  stderr=subprocess.STDOUT,
                                  env=host_environment(gpu), timeout=REPORT_TIMEOUT)
    except subprocess.TimeoutExpired:
        return {"status": "error", "failures": [{"case": api, "detail": f"ran past {REPORT_TIMEOUT // 60} min"}],
                "log": str(log)}
    summary = out / "summary.json"
    if not summary.exists():
        tail = log.read_text(errors="replace").strip().splitlines()[-5:]
        return {"status": "error", "log": str(log),
                "failures": [{"case": api, "detail": f"no summary (exit {done.returncode}): {' | '.join(tail)}"}]}
    failures, notes, count = judge_summary(summary, api, gpu.name)
    if count == 0:
        failures.append({"case": api, "detail": "the suite ran no sample"})
    return {"status": "fail" if failures else "pass", "failures": failures, "notes": notes, "samples": count,
            "log": str(log), "report": str(out / "index.html")}


# ------------------------------------------------------------------- one GPU

class LogTail:
    """The bytes a host log gained while a GPU's pass ran."""

    def __init__(self, path):
        self.path = path
        self.start = path.stat().st_size if path is not None and path.exists() else 0

    def save(self, target):
        if self.path is None or not self.path.exists():
            return
        with open(self.path, "rb") as source:
            size = source.seek(0, os.SEEK_END)
            source.seek(self.start if size >= self.start else 0)
            target.write_bytes(source.read())


def guest_clock(guest):
    """The guest's own UTC time, as the cut-off for the logs a suite wrote: the
    host's clock is no measure of the guest's file times, since nothing sets
    the guest's clock or time zone while the manager is closed."""
    try:
        code, output = guest.powershell("(Get-Date).ToUniversalTime().ToString('o')", 60)
    except (AgentError, OSError):
        return ""
    lines = output.strip().splitlines()
    return lines[-1].strip() if code == 0 and lines else ""


def collect_guest_logs(guest, target, since):
    archive = "C:\\av\\gputest-logs.zip"
    after = f"[DateTime]::Parse('{since}').ToUniversalTime()" if since else "[DateTime]::MinValue"
    script = (f"Remove-Item {archive} -ErrorAction SilentlyContinue; "
              f"$files = Get-ChildItem C:\\ProgramData\\av\\*.log -ErrorAction SilentlyContinue | "
              f"Where-Object {{ $_.LastWriteTimeUtc -ge {after} }}; "
              f"if ($files) {{ Compress-Archive -Path $files.FullName -DestinationPath {archive} }}")
    try:
        guest.powershell(script, 300)
        guest.agent.pull(archive, target)
    except (AgentError, OSError):
        target.unlink(missing_ok=True)


def crashed(result, name):
    """The VM was gone after `name`: QEMU died under it, and the verdict says
    so whatever the suite's own tool made of the silence."""
    say(f"the VM is not running after {name}: QEMU died during it")
    entry = {"case": f"{name}: the VM", "detail": "QEMU exited during the suite (see qemu.log and the host's "
                                                  "coredumpctl)"}
    keys = [key for key in result["suites"] if key == name or key.startswith(f"probe-")] if name == "probes" \
        else [name]
    for key in keys or [name]:
        verdict = result["suites"].setdefault(key, {"status": "error", "failures": []})
        verdict["status"] = "error"
        verdict["failures"] = [entry] + verdict.get("failures", [])


def gpu_pass(domain, original, share, gpu, args, out):
    """One GPU: the VM on it alone, its adapter bound, every suite run."""
    out.mkdir(parents=True, exist_ok=True)
    work = bridge_directory(gpu.index)
    result = {"gpu": {"index": gpu.index, "name": gpu.name, "uuid": gpu.uuid, "vendor": f"{gpu.vendor:04x}",
                      "device": f"{gpu.device:04x}", "kind": gpu.kind},
              "started": datetime.datetime.now().isoformat(timespec="seconds"), "suites": {}}
    tails = {"avgpu.log": LogTail(avgpu_log_path(original)),
             "qemu.log": LogTail(pathlib.Path.home() / ".cache" / "libvirt" / "qemu" / "log" / f"{DOMAIN}.log")}
    since = ""
    guest = None
    try:
        domain.stop()
        domain.define(retarget(original, [gpu], args.qemu_env))
        guest = boot(domain, work, share)
        adapter, status = adapter_state(guest, gpu)
        if not bound(adapter):
            say(f"{gpu.name}: the adapter is not on avgpu ({adapter}); signing the package for it")
            run_install(guest, [], out / "driver-bind.log")
            guest.close()
            domain.stop()
            guest = boot(domain, work, share)
            adapter, status = adapter_state(guest, gpu)
        (out / "gpu_status.json").write_text(json.dumps(status, indent=1))
        result["adapter"] = adapter
        if not bound(adapter):
            raise RunFailed(f"the guest's adapter for {gpu.name} is not running on avgpu: {adapter}")
        fresh = True
        for name in [suite_name for suite_name in SUITES + APP_SUITES + TRACE_SUITES if suite_name in args.suites]:
            if guest is None or (args.reboot_between and not fresh):
                if guest is not None:
                    guest.close()
                    domain.stop()
                guest = boot(domain, work, share)
            fresh = False
            since = guest_clock(guest)
            user = console_user(guest, args.guest_user)
            if name == "probes":
                result["suites"].update({f"probe-{api}": verdict
                                         for api, verdict in
                                         run_probes(guest, out / "probes", args.device_loss).items()})
            elif name == "clgl":
                result["suites"]["clgl"] = run_clgl(work, out / "clgl", user)
            elif name in APP_SUITES:
                result["suites"][name] = run_app_suite(name, guest, out / name, gpu, adapter.get("name", ""))
            elif name in TRACE_SUITES:
                result["suites"][name] = run_trace_suite(guest, out / name, gpu, adapter.get("name", ""), args.traces)
            else:
                result["suites"][name] = run_report(name, work, out / name, args, gpu, user)
            if not domain.running():
                crashed(result, name)
                guest.close()
                guest = None
                continue
            collect_guest_logs(guest, out / f"guest-logs-{name}.zip", since)
    except (RunFailed, LinkError, AgentError, OSError, subprocess.SubprocessError) as error:
        say(f"{gpu.name}: {error}")
        result["error"] = str(error)
        (out / "traceback.txt").write_text(traceback.format_exc())
    finally:
        if guest is not None:
            guest.close()
        for name, tail in tails.items():
            tail.save(out / name)
    result["finished"] = datetime.datetime.now().isoformat(timespec="seconds")
    (out / "results.json").write_text(json.dumps(result, indent=1))
    return result


# ------------------------------------------------------------------ commands

def selected(gpus, selection):
    if not selection:
        return gpus
    wanted = [part.strip() for part in selection.split(",") if part.strip()]
    # A number is an index, never part of a name: "1" is in "GFX1201".
    return [gpu for gpu in gpus if any(part == str(gpu.index) if part.isdigit()
                                       else part.lower() in gpu.name.lower() or part.lower() == gpu.uuid
                                       for part in wanted)]


def write_status(out, **fields):
    (out / "status.json").write_text(json.dumps(fields, indent=1))


def loss_layer_environment(out):
    """QEMU's environment for VK_LAYER_AV_device_loss, the test layer that
    loses a host device on request: the D3D11 probe's loss steps lose its
    device and judge that the guest is told and recovers. Its manifest is
    written here, naming the library where this host has it."""
    manifest = out / "loss-layer" / "VkLayer_av_device_loss.json"
    manifest.parent.mkdir(parents=True, exist_ok=True)
    text = (AV / "tests" / "gpu_fault" / "VkLayer_av_device_loss.json.in").read_text()
    manifest.write_text(text.replace("@AV_LOSS_LAYER_LIBRARY@", str(LOSS_LAYER)))
    return [f"VK_ADD_LAYER_PATH={manifest.parent}", "VK_INSTANCE_LAYERS=VK_LAYER_AV_device_loss"]


def command_run(args):
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    problems = preflight(args.allow_manager)
    if problems:
        write_status(out, state="failed", reason="; ".join(problems))
        for problem in problems:
            say(f"preflight: {problem}")
        return 2
    args.suites = [name.strip() for name in args.suites.split(",") if name.strip()]
    unknown = [name for name in args.suites if name not in SUITES + APP_SUITES + TRACE_SUITES]
    if unknown:
        say(f"unknown suite(s): {', '.join(unknown)}; known: {', '.join(SUITES)}")
        return 2
    all_gpus = host_gpus()
    gpus = selected(all_gpus, args.gpus)
    if not gpus:
        say(f"no GPU matches {args.gpus}")
        return 2
    if args.device_loss:
        args.qemu_env += loss_layer_environment(out)
    domain = Domain()
    STATE.mkdir(parents=True, exist_ok=True)
    if SAVED_DOMAIN.exists():
        say(f"{SAVED_DOMAIN} is left from a run that did not finish: it is the definition put back")
    else:
        SAVED_DOMAIN.write_text(domain.definition())
    original = SAVED_DOMAIN.read_text()
    was_running = domain.running()
    share = GuestShare(args.user, args.password)
    host = os.uname().nodename
    write_status(out, state="running", host=host, gpus=[gpu.name for gpu in gpus], current="driver")
    say(f"{host}: {len(gpus)} GPU(s): {', '.join(gpu.name for gpu in gpus)}")
    results = []
    try:
        if not args.skip_driver:
            say("driver: the VM on every GPU, to install the package for all of them")
            domain.stop()
            domain.define(retarget(original, all_gpus, args.qemu_env))
            guest = boot(domain, bridge_directory("driver"), share)
            try:
                update_driver(guest, all_gpus, out, args.force_driver)
            finally:
                guest.close()
        for gpu in gpus:
            write_status(out, state="running", host=host, gpus=[g.name for g in gpus], current=gpu.name)
            say(f"=== {gpu.name} ({gpu.uuid})")
            results.append(gpu_pass(domain, original, share, gpu, args, out / slug(gpu)))
    except (RunFailed, LinkError, AgentError, OSError, subprocess.SubprocessError) as error:
        say(f"run stopped: {error}")
        write_status(out, state="failed", host=host, reason=str(error))
        (out / "traceback.txt").write_text(traceback.format_exc())
        return 1
    finally:
        say("putting the VM's own definition back")
        domain.stop()
        domain.define(original)
        SAVED_DOMAIN.unlink(missing_ok=True)
        if was_running:
            domain.start()
    failed = sum(1 for result in results
                 if result.get("error") or any(s["status"] != "pass"
                                               for s in result["suites"].values()))
    write_status(out, state="done", host=host, gpus=[gpu.name for gpu in gpus], failed_gpus=failed)
    say(f"done: {len(results) - failed} of {len(results)} GPU(s) passed everything; results in {out}")
    return 0


def command_hold(args):
    """The VM on one GPU with this tree's build, its bridge at
    bridge_directory("hold")/agent.sock for accel-virt's guest tools, until
    SIGTERM or SIGINT; then the VM's own definition again."""
    import signal
    gpus = selected(host_gpus(), args.gpu)
    if len(gpus) != 1:
        say(f"--gpu {args.gpu} matches {len(gpus)} GPUs; it must match one")
        return 2
    domain = Domain()
    STATE.mkdir(parents=True, exist_ok=True)
    if not SAVED_DOMAIN.exists():
        SAVED_DOMAIN.write_text(domain.definition())
    original = SAVED_DOMAIN.read_text()
    was_running = domain.running()
    stop = []
    signal.signal(signal.SIGTERM, lambda *_: stop.append(True))
    signal.signal(signal.SIGINT, lambda *_: stop.append(True))
    guest = None
    try:
        domain.stop()
        domain.define(retarget(original, gpus, args.qemu_env))
        guest = boot(domain, bridge_directory("hold"), GuestShare(args.user, args.password))
        say(f"holding the VM on {gpus[0].name}: {guest.work / 'agent.sock'}")
        while not stop:
            time.sleep(1)
    finally:
        if guest is not None:
            guest.close()
        say("putting the VM's own definition back")
        domain.stop()
        domain.define(original)
        SAVED_DOMAIN.unlink(missing_ok=True)
        if was_running:
            domain.start()
    return 0


def command_restore(_args):
    if not SAVED_DOMAIN.exists():
        say("nothing to restore")
        return 0
    domain = Domain()
    domain.stop()
    domain.define(SAVED_DOMAIN.read_text())
    SAVED_DOMAIN.unlink()
    say("the VM's own definition is back")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("preflight", help="what this host lacks for a run")
    check.add_argument("--allow-manager", action="store_true")
    commands.add_parser("gpus", help="the host GPUs a run would test")
    commands.add_parser("restore", help="put back the VM definition a killed run left changed")
    held = commands.add_parser("hold", help="the VM on one GPU, its bridge serving, until SIGTERM")
    held.add_argument("--gpu", required=True, help="an index, UUID or part of a name")
    held.add_argument("--user", default="lsw")
    held.add_argument("--password", default="lsw")
    held.add_argument("--qemu-env", action="append", default=[], metavar="NAME=VALUE",
                      help="set in QEMU's environment (repeatable)")
    run = commands.add_parser("run", help="every suite on every GPU")
    run.add_argument("--out", type=pathlib.Path, required=True)
    run.add_argument("--gpus", default="", help="comma-separated indices, UUIDs or parts of names (default: all)")
    run.add_argument("--suites", default=",".join(SUITES),
                     help=f"comma-separated, of {', '.join(SUITES)} (the default) and the applications "
                          f"{', '.join(APP_SUITES)} and {', '.join(TRACE_SUITES)}")
    run.add_argument("--traces", default="", help="the traces suite's traces: comma-separated apps, APP/FUNC or patterns (resolve/fx-*) "
                                                  "(default: every one in the store)")
    run.add_argument("--frames", type=int, default=30, help="timed frames per sample: correctness, not speed")
    run.add_argument("--warmup", type=int, default=5)
    run.add_argument("--user", default="lsw", help="the guest account the share is opened as")
    run.add_argument("--password", default="lsw")
    run.add_argument("--guest-user", default="lsw", help="the logged-on user the suites' tasks run as")
    run.add_argument("--force-driver", action="store_true", help="install the package even if the guest has it")
    run.add_argument("--skip-driver", action="store_true", help="leave the guest's driver package as it is")
    run.add_argument("--allow-manager", action="store_true")
    run.add_argument("--qemu-env", action="append", default=[], metavar="NAME=VALUE",
                     help="set in QEMU's environment for this run (repeatable)")
    run.add_argument("--no-device-loss", dest="device_loss", action="store_false",
                     help="leave out the test layer that loses a host device on request, and so the "
                          "D3D11 probe's loss steps")
    run.add_argument("--no-reboot-between", dest="reboot_between", action="store_false",
                     help="run every suite in one boot instead of booting the VM afresh for each")
    args = parser.parse_args()
    if args.command == "preflight":
        problems = preflight(args.allow_manager)
        for problem in problems:
            print(f"preflight: {problem}")
        print("preflight: ok" if not problems else f"preflight: {len(problems)} problem(s)")
        sys.exit(1 if problems else 0)
    if args.command == "gpus":
        print(list_gpus.table(host_gpus()))
        sys.exit(0)
    if args.command == "hold":
        sys.exit(command_hold(args))
    if args.command == "restore":
        sys.exit(command_restore(args))
    sys.exit(command_run(args))


if __name__ == "__main__":
    main()
