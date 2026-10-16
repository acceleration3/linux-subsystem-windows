"""Real applications on one GPU, in the guest gpu_matrix.py booted on it.

  wgc       remotewin-wgcbench in its four forms (a Direct3D 11 or a GDI
            window, the monitor or the window captured), judged by its own
            PASS line, and the desktop still composed after each
  affinity  Affinity (Canva), installed from its MSIX: the adapters and the
            OpenCL device its log says it found, and its window drawn by WPF
  resolve   DaVinci Resolve: the OpenCL device gpudetect.bin says it chose
  hl2       Half-Life 2 (Steam 220): sixty seconds of the train ride that
            opens d1_trainstation_01, which plays itself, timed by PresentMon
  dsr       Dark Souls Remastered (Steam 570940): its title screen and
            menus, timed by PresentMon

The applications are the user's own installs in the VM (Steam signed in,
Canva signed in where its pages need it): a suite whose application is
missing fails and says so. Every suite also fails on a GPU reset during it
(a LiveKernelEvent or a display driver event 4101 in the guest's logs).

Programs with windows run in the console session through a scheduled task
whose launcher shows no window of its own, so nothing on the desktop takes
the focus from the application; screenshots and input are guest/shot.ps1
and guest/input.ps1.
"""
import csv
import io
import json
import pathlib
import re
import statistics
import time

import run_guest_suite as suite
from guest_agent import AgentError
from lsw_link import LinkError

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[1]
ASSETS = HERE / "guest"
GUEST_APPS = "C:\\av\\apps"
WGC_BENCH = TREE / "build-agent" / "remotewin" / "source" / "guest" / "remotewin-wgcbench.exe"
PRESENTMON_URL = "https://github.com/GameTechDev/PresentMon/releases/download/v2.3.0/PresentMon-2.3.0-x64.exe"
STEAM = "C:\\Program Files (x86)\\Steam"
APP_SUITES = ("wgc", "affinity", "resolve", "hl2", "dsr")
WGC_FORMS = (("d3d", "monitor"), ("gdi", "monitor"), ("d3d", "window"), ("gdi", "window"))
WGC_SECONDS = 15
DARK_TASKBAR = 20.0
"""A taskbar darker than this on average is not being composed."""
BLACK_WINDOW = 0.2
"""A window with less than this share of lit pixels is not being drawn."""
GAME_SECONDS = 60
GROWTH_MIB = 512
"""What a game may commit while it plays one scene: streaming, not a leak. Dark
Souls Remastered leaking 1 MiB a frame grew some 3.7 GB in a minute."""
SETTLE_SECONDS = 60
"""How long a fresh boot is left alone before a timed suite: Windows' own
start-up work (indexing, updates, the shell's first paint) shows in a capture
benchmark as gaps of its own."""


def say(line):
    print(f"[{time.strftime('%H:%M:%S')}] {line}", flush=True)


class HiddenConsole(suite.ConsoleProgram):
    """ConsoleProgram whose task starts its script through hidden.vbs, so no
    console window appears over (and takes the focus from) the desktop."""

    task = "avapps"
    script = GUEST_APPS + "\\console.cmd"

    def run(self, path, guest_output, step):
        user = self.console_user()
        self.agent.push(f"@echo off\r\n{path} > {guest_output} 2>&1\r\n".encode(), self.script)
        self.schtasks(["/create", "/tn", self.task, "/tr",
                       f"wscript.exe //B {GUEST_APPS}\\hidden.vbs {self.script}", "/sc", "once", "/st", "23:59",
                       "/ru", user, "/it", "/f"], step)
        try:
            self.schtasks(["/run", "/tn", self.task], step)
            return self.finished(step)
        finally:
            self.agent.run(suite.SCHTASKS, ["/delete", "/tn", self.task, "/f"], self.timeout)


class Desktop:
    """The logged-on user's desktop in one boot of the guest: programs run in
    it, its picture read back, input sent to it."""

    def __init__(self, guest, out, guest_name):
        self.guest = guest
        self.out = out
        self.guest_name = guest_name
        self.runs = 0
        out.mkdir(parents=True, exist_ok=True)

    def prepare(self):
        self.guest.share.make_directories(GUEST_APPS)
        self.powershell(f"Remove-Item '{GUEST_APPS}\\console-out-*.txt' -ErrorAction SilentlyContinue", 60)
        for asset in ("shot.ps1", "input.ps1", "hidden.vbs"):
            self.guest.share.put(ASSETS / asset, f"{GUEST_APPS}\\{asset}")

    def run(self, command, timeout=120):
        """`command` in the console session: its exit code and output.

        A file of its own each run: a program the command started keeps the
        redirection open, and the next run's redirection into it would fail."""
        self.runs += 1
        output = f"{GUEST_APPS}\\console-out-{self.runs}.txt"
        code = HiddenConsole(self.guest.agent, timeout).run(command, output, "desktop")
        try:
            text = self.guest.share.get_bytes(output).decode(errors="replace")
        except LinkError:
            text = ""
        return code, text

    def start(self, command):
        """`command` started in the console session without waiting for it."""
        return self.run(f'start "" {command}', 60)

    def powershell(self, script, timeout=300):
        return self.guest.powershell(script, timeout)

    def screenshot(self, name, process=None):
        """The screen or `process`'s window into out/NAME.png, and what
        shot.ps1 measured of it ({"lit": .., "taskbar": ..})."""
        guest_png = f"{GUEST_APPS}\\{name}.png"
        _, text = self.run(f"powershell -NoProfile -ExecutionPolicy Bypass -File {GUEST_APPS}\\shot.ps1 "
                           f"{guest_png}" + (f" {process}" if process else ""), 120)
        measured = {key: float(value) for key, value in re.findall(r"^(lit|taskbar) ([\d.]+)", text, re.M)}
        if not measured:
            (self.out / f"{name}.shot.txt").write_text(text)
        try:
            self.guest.share.get(guest_png, self.out / f"{name}.png")
        except LinkError:
            pass
        return measured

    def input(self, *arguments):
        return self.run(f"powershell -NoProfile -ExecutionPolicy Bypass -File {GUEST_APPS}\\input.ps1 "
                        + " ".join(arguments), 60)

    def kill(self, *names):
        self.powershell("Stop-Process -Force -ErrorAction SilentlyContinue -Name " + ",".join(names), 120)

    def clock(self):
        _, text = self.powershell("(Get-Date).ToUniversalTime().ToString('o')", 60)
        return text.strip().splitlines()[-1] if text.strip() else ""

    def resets_since(self, since):
        """GPU resets the guest had after `since`: the dumps Windows writes
        for a TDR or another live kernel event, and display driver 4101s.
        Not Windows Error Reporting's 1001s - it sends old reports again
        after a reboot, dated when sent."""
        after = f"[DateTime]::Parse('{since}').ToUniversalTime()" if since else "(Get-Date).AddHours(-1).ToUniversalTime()"
        _, text = self.powershell(
            f"$after = {after}; "
            "Get-ChildItem C:\\Windows\\LiveKernelReports -Recurse -Filter *.dmp -ErrorAction SilentlyContinue | "
            "? { $_.CreationTimeUtc -gt $after } | % { 'live kernel event ' + $_.Name + ' at ' + $_.CreationTime }; "
            "Get-WinEvent -FilterHashtable @{LogName='System'; Id=4101; StartTime=$after.ToLocalTime()} "
            "-ErrorAction SilentlyContinue | % { 'display driver reset at ' + $_.TimeCreated }", 120)
        return [line.strip() for line in text.splitlines() if line.strip()]

    def presentmon(self, process, seconds, name):
        """PresentMon on `process` for `seconds`: frame times in ms."""
        exe = f"{GUEST_APPS}\\PresentMon.exe"
        if not self.guest.share.exists(exe):
            self.powershell(f"$ProgressPreference='SilentlyContinue'; Invoke-WebRequest {PRESENTMON_URL} -OutFile {exe}",
                            600)
        guest_csv = f"{GUEST_APPS}\\{name}.csv"
        self.powershell(f"Remove-Item {guest_csv} -ErrorAction SilentlyContinue", 60)
        self.guest.run(exe, ["--process_name", process, "--timed", str(seconds), "--terminate_after_timed",
                             "--output_file", guest_csv, "--stop_existing_session", "--no_console_stats"],
                       seconds + 120)
        try:
            text = self.guest.share.get_bytes(guest_csv).decode(errors="replace")
        except LinkError:
            return [], {}
        (self.out / f"{name}.csv").write_text(text)
        rows = list(csv.DictReader(io.StringIO(text)))
        times = [float(row["FrameTime"]) for row in rows if row.get("FrameTime") not in (None, "", "NA")]
        mode = rows[-1].get("PresentMode", "") if rows else ""
        return times, {"present_mode": mode, "runtime": rows[-1].get("PresentRuntime", "") if rows else ""}


def frame_metrics(times):
    """Average fps, 1% low fps and the 99th percentile frame time of a run."""
    if not times:
        return {}
    ordered = sorted(times)
    worst = ordered[int(len(ordered) * 0.99):] or ordered[-1:]
    return {"frames": len(times), "fps": round(1000.0 / statistics.mean(times), 1),
            "fps_1pct_low": round(1000.0 / statistics.mean(worst), 1),
            "p99_ms": round(ordered[min(len(ordered) - 1, int(len(ordered) * 0.99))], 1)}


def names_gpu(found, gpu, guest_name):
    """Whether a name an application reported is this GPU: the guest's own
    name for the adapter ("AMD Radeon(TM) Graphics" for a Radeon 610M), or
    the host's."""
    found = found.lower()
    return any(name and (name in found or found in name)
               for name in (guest_name.lower(), gpu.name.split(" (")[0].lower()))


def verdict(failures, notes=(), metrics=None, log=None):
    result = {"status": "fail" if failures else "pass", "failures": list(failures), "notes": list(notes)}
    if metrics:
        result["metrics"] = metrics
    if log:
        result["log"] = str(log)
    return result


# ------------------------------------------------------------------ the suites

def run_wgc(desktop, gpu):
    desktop.guest.share.put(WGC_BENCH, f"{GUEST_APPS}\\wgcbench.exe")
    time.sleep(SETTLE_SECONDS)
    failures, metrics, logs = [], {}, []
    for drawn, captured in WGC_FORMS:
        form = f"{drawn}-{captured}"
        say(f"wgc {form}")
        code, text = desktop.run(f"{GUEST_APPS}\\wgcbench.exe {WGC_SECONDS} {drawn} {captured}", WGC_SECONDS * 4 + 60)
        logs.append(f"=== {form} (exit {code})\n{text}")
        rate = re.search(r"frames (\d+) in ([\d.]+) s: ([\d.]+) fps", text)
        gaps = re.search(r"worst ([\d.]+);", text)
        if rate:
            metrics[f"{form}_fps"] = float(rate.group(3))
        if gaps:
            metrics[f"{form}_worst_gap_ms"] = float(gaps.group(1))
        if not re.search(r"^PASS\r?$", text, re.M):
            failures.append({"case": form, "detail": (rate.group(0) if rate else f"exit {code}")
                             + (f", worst gap {gaps.group(1)} ms" if gaps else "")})
        shown = desktop.screenshot(f"wgc-after-{form}")
        if shown.get("taskbar", 0.0) < DARK_TASKBAR:
            failures.append({"case": f"{form}: the desktop",
                             "detail": f"not composed after the run (taskbar brightness {shown.get('taskbar')})"})
            break
    (desktop.out / "wgc.log").write_text("\n".join(logs))
    return verdict(failures, metrics=metrics, log=desktop.out / "wgc.log")


def affinity_session(text):
    """The last session in Affinity's Log.txt: what it found, by section."""
    session = text[text.rfind("] Affinity 3"):] if "] Affinity 3" in text else text
    devices = re.findall(r"\[OpenCL\] Found \d+ devices for platform ([^\r\n:]+):\r?\n([^\r\n]+)", session)
    dxgi = re.search(r"\[DXGI\] Enumerating adapters\r?\n(.*?)(?:\r?\n\[|\Z)", session, re.S)
    adapters = [line.strip() for line in (dxgi.group(1) if dxgi else "").splitlines()
                if line.strip() and not line.startswith(("\t", " "))]
    return devices, adapters


def run_affinity(desktop, gpu):
    _, installed = desktop.powershell("(Get-AppxPackage -AllUsers Canva.Affinity).PackageFamilyName", 120)
    family = installed.strip().splitlines()[-1].strip() if installed.strip() else ""
    if not family.startswith("Canva.Affinity"):
        return verdict([{"case": "install", "detail": "Affinity is not installed in this VM"}])
    desktop.kill("Affinity")
    log = "C:\\Users\\lsw\\.affinity\\Affinity\\3.0\\Log.txt"
    desktop.start(f"explorer.exe shell:AppsFolder\\{family}!Canva.Affinity")
    time.sleep(45)
    shown = desktop.screenshot("affinity", "Affinity")
    try:
        text = desktop.guest.share.get_bytes(log).decode(errors="replace")
    except LinkError:
        text = ""
    (desktop.out / "affinity-log.txt").write_text(text)
    desktop.kill("Affinity")
    devices, adapters = affinity_session(text)
    failures = []
    if not any(names_gpu(device, gpu, desktop.guest_name) for _, device in devices):
        failures.append({"case": "opencl", "detail": f"OpenCL devices found: {devices or 'none'}"})
    if not any(names_gpu(adapter, gpu, desktop.guest_name) for adapter in adapters):
        failures.append({"case": "dxgi", "detail": f"DXGI adapters found: {adapters or 'none'}"})
    if shown.get("lit", 0.0) < BLACK_WINDOW:
        failures.append({"case": "window", "detail": f"its window is not drawn ({shown.get('lit', 0.0):.0%} lit)"})
    return verdict(failures, notes=[{"case": "found", "detail": f"OpenCL {devices}; DXGI {adapters}"}],
                   log=desktop.out / "affinity-log.txt")


def run_resolve(desktop, gpu):
    resolve = "C:\\Program Files\\Blackmagic Design\\DaVinci Resolve\\Resolve.exe"
    if not desktop.guest.share.exists(resolve):
        return verdict([{"case": "install", "detail": "DaVinci Resolve is not installed in this VM"}])
    detect = "C:\\Users\\lsw\\AppData\\Roaming\\Blackmagic Design\\DaVinci Resolve\\Support\\logs\\gpudetect.bin"
    desktop.kill("Resolve")
    desktop.powershell(f"Remove-Item '{detect}' -ErrorAction SilentlyContinue", 60)
    desktop.start(f'"{resolve}"')
    time.sleep(75)
    shown = desktop.screenshot("resolve", "Resolve")
    try:
        found = desktop.guest.share.get_bytes(detect)
    except LinkError:
        found = b""
    desktop.kill("Resolve")
    strings = [text.decode(errors="replace") for text in re.findall(rb"[\x20-\x7e]{4,}", found)]
    (desktop.out / "resolve-gpudetect.txt").write_text("\n".join(strings))
    platforms = sorted({text for text in strings if text.startswith("avgpu OpenCL on ")})
    failures = []
    if not platforms:
        failures.append({"case": "opencl", "detail": "gpudetect.bin names no avgpu OpenCL platform"
                                                     + ("" if found else " (no gpudetect.bin)")})
    elif not any(names_gpu(platform.removeprefix("avgpu OpenCL on "), gpu, desktop.guest_name)
                 for platform in platforms):
        failures.append({"case": "opencl", "detail": f"its OpenCL platforms are {platforms}"})
    if shown.get("lit", 0.0) < BLACK_WINDOW:
        failures.append({"case": "window", "detail": f"its window is not drawn ({shown.get('lit', 0.0):.0%} lit)"})
    return verdict(failures, notes=[{"case": "found", "detail": f"{platforms}"}],
                   log=desktop.out / "resolve-gpudetect.txt")


def steam_ready(desktop, app):
    manifest = f"{STEAM}\\steamapps\\appmanifest_{app}.acf"
    if not desktop.guest.share.exists(f"{STEAM}\\config\\loginusers.vdf"):
        return "Steam is not signed in in this VM"
    try:
        text = desktop.guest.share.get_bytes(manifest).decode(errors="replace")
    except LinkError:
        return f"Steam app {app} is not installed in this VM"
    flags = re.search(r'"StateFlags"\s+"(\d+)"', text)
    if not flags or int(flags.group(1)) & 4 == 0:
        return f"Steam app {app} is not fully installed (StateFlags {flags.group(1) if flags else '?'})"
    return None


def run_game(desktop, app, process, arguments, ready, name):
    """Steam app `app` launched with `arguments`, `ready(desktop)` waited for,
    then timed for GAME_SECONDS."""
    missing = steam_ready(desktop, app)
    if missing:
        return verdict([{"case": "install", "detail": missing}])
    desktop.kill(process)
    since = desktop.clock()
    desktop.start(f'"{STEAM}\\steam.exe" -applaunch {app} {arguments}')
    failures = []
    if not ready(desktop):
        failures.append({"case": "start", "detail": f"{process} never reached the scene it is timed in"})
    before = private_bytes(desktop, process)
    times, how = desktop.presentmon(f"{process}.exe", GAME_SECONDS, name) if not failures else ([], {})
    after = private_bytes(desktop, process)
    shown = desktop.screenshot(name)
    if not failures and after is None:
        failures.append({"case": "run", "detail": f"{process} exited during the timed run"})
    growth = None if before is None or after is None else (after - before) / 2**20
    if growth is not None and growth > GROWTH_MIB:
        failures.append({"case": "memory", "detail": f"{process} grew {growth:.0f} MiB in {GAME_SECONDS} s"})
    if not failures and len(times) < GAME_SECONDS:
        failures.append({"case": "frames", "detail": f"{len(times)} frames in {GAME_SECONDS} s"})
    desktop.kill(process)
    for reset in desktop.resets_since(since):
        failures.append({"case": "gpu reset", "detail": reset})
    metrics = dict(frame_metrics(times), **how)
    if growth is not None:
        metrics["memory_growth_mib"] = round(growth, 1)
    if shown.get("lit") is not None:
        metrics["screen_lit"] = shown["lit"]
    return verdict(failures, metrics=metrics)


def private_bytes(desktop, process):
    """What `process` has committed for itself, or None when it is not running."""
    _, text = desktop.powershell(f"(Get-Process {process} -ErrorAction SilentlyContinue"
                                 " | Select-Object -First 1).PrivateMemorySize64", 60)
    text = text.strip()
    return int(text) if text.isdigit() else None


def hl2_ready(desktop):
    """The map is in: the train ride runs from here by itself."""
    log = f"{STEAM}\\steamapps\\common\\Half-Life 2\\hl2\\console.log"
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        time.sleep(10)
        try:
            text = desktop.guest.share.get_bytes(log).decode(errors="replace")
        except LinkError:
            continue
        if "Redownloading all lightmaps" in text:
            time.sleep(5)
            return True
    return False


def run_hl2(desktop, gpu):
    log = f"{STEAM}\\steamapps\\common\\Half-Life 2\\hl2\\console.log"
    desktop.powershell(f"Remove-Item '{log}' -ErrorAction SilentlyContinue", 60)
    return run_game(desktop, 220, "hl2",
                    "-novid -fullscreen -w 1920 -h 1080 -condebug +engine_no_focus_sleep 0 +fps_max 0 "
                    "+mat_vsync 0 +map d1_trainstation_01", hl2_ready, "hl2")


def dsr_ready(desktop):
    """The title screen, after the logos."""
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        time.sleep(10)
        _, alive = desktop.powershell("(Get-Process DarkSoulsRemastered -ErrorAction SilentlyContinue).Id", 60)
        if alive.strip():
            time.sleep(30)
            return True
    return False


def run_dsr(desktop, gpu):
    return run_game(desktop, 570940, "DarkSoulsRemastered", "", dsr_ready, "dsr")


RUNNERS = {"wgc": run_wgc, "affinity": run_affinity, "resolve": run_resolve, "hl2": run_hl2, "dsr": run_dsr}


def run_app_suite(name, guest, out, gpu, guest_name):
    """Suite `name` on `gpu`, whose adapter the guest calls `guest_name`."""
    say(f"suite {name}")
    desktop = Desktop(guest, out, guest_name)
    try:
        desktop.prepare()
        since = desktop.clock()
        result = RUNNERS[name](desktop, gpu)
        if name in ("wgc", "affinity", "resolve"):
            for reset in desktop.resets_since(since):
                result["failures"].append({"case": "gpu reset", "detail": reset})
                result["status"] = "fail"
    except (AgentError, LinkError, suite.StepFailed, OSError) as error:
        result = verdict([{"case": name, "detail": f"{type(error).__name__}: {error}"}])
        result["status"] = "error"
    (out / "verdict.json").write_text(json.dumps(result, indent=1))
    return result
