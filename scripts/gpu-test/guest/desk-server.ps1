# desk-server.ps1: an elevated helper in the console session that runs the
# PowerShell snippets desk.py drops into -Dir, one at a time, and leaves each
# one's output beside it. Started by desk.py from a scheduled task made with
# /rl highest, so the input it sends reaches an elevated application (UIPI
# drops input a lower-integrity sender sends to a higher one: Resolve traced
# through apitrace's wrapper runs elevated), and what it starts is elevated.
#
# A snippet is <n>.ps1 (desk.py writes <n>.txt and renames it, so a snippet is
# never read half-written); its output goes to <n>.out the same way. The
# helpers below (Click, Drag, Press, Keys, Shot, Windows...) are defined once.
# The server ends when <Dir>\stop appears.
param([string]$Dir = 'C:\av\apps\desk')
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class Desk {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, int d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  public struct RECT { public int L, T, R, B; }
  public static List<string> Windows() {
    var list = new List<string>();
    EnumWindows((h, l) => {
      if (!IsWindowVisible(h)) return true;
      RECT r; GetWindowRect(h, out r); if (r.R - r.L < 40 || r.B - r.T < 20) return true;
      uint p; GetWindowThreadProcessId(h, out p); var s = new StringBuilder(256); GetWindowText(h, s, 256);
      list.Add(String.Format("0x{0:x} pid {1} [{2},{3} {4}x{5}] {6}", h.ToInt64(), p, r.L, r.T, r.R - r.L, r.B - r.T, s));
      return true; }, IntPtr.Zero);
    return list;
  }
  // The first visible top-level window of process `pid` titled exactly `title`: its handle and rectangle.
  public static long[] Find(uint pid, string title) {
    long[] found = null;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true;
      var s = new StringBuilder(256); GetWindowText(h, s, 256);
      if (s.ToString() != title) return true;
      RECT r; GetWindowRect(h, out r); found = new long[] { h.ToInt64(), r.L, r.T, r.R, r.B }; return false; }, IntPtr.Zero);
    return found;
  }
  public static void Key(byte vk, bool up) {
    byte sc = (byte)MapVirtualKey(vk, 0);
    uint ext = ((vk >= 0x21 && vk <= 0x2E) || vk == 0x5B) ? 1u : 0u;
    keybd_event(vk, sc, ext | (up ? 2u : 0u), IntPtr.Zero);
  }
}
"@
[Desk]::SetProcessDPIAware() | Out-Null
$Keys = @{ enter = 0x0D; esc = 0x1B; space = 0x20; left = 0x25; up = 0x26; right = 0x27; down = 0x28; tab = 0x09
           backspace = 0x08; delete = 0x2E; home = 0x24; end = 0x23; pageup = 0x21; pagedown = 0x22
           ctrl = 0x11; shift = 0x10; alt = 0x12; win = 0x5B
           f1 = 0x70; f2 = 0x71; f3 = 0x72; f4 = 0x73; f5 = 0x74; f6 = 0x75; f7 = 0x76; f8 = 0x77; f9 = 0x78; f10 = 0x79; f11 = 0x7A; f12 = 0x7B }
function Vk($name) {
    $k = "$name".ToLower()
    if ($Keys.ContainsKey($k)) { return [byte]$Keys[$k] }
    if ($k.Length -eq 1) { return [byte][int][char]$k.ToUpper() }
    [byte][int]$k
}
function Move-To($x, $y) { [Desk]::SetCursorPos([int]$x, [int]$y) | Out-Null }
function Click($x, $y, [switch]$Right) {
    Move-To $x $y; Start-Sleep -Milliseconds 120
    $down, $up = if ($Right) { 8, 16 } else { 2, 4 }
    [Desk]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60; [Desk]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero)
}
function DClick($x, $y) {
    Move-To $x $y; Start-Sleep -Milliseconds 120
    1..2 | ForEach-Object { [Desk]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); [Desk]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 70 }
}
# A press at one point, the pointer moved in steps, a release at another.
function Drag($x1, $y1, $x2, $y2, $steps = 20, $ms = 25) {
    Move-To $x1 $y1; Start-Sleep -Milliseconds 150
    [Desk]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 150
    for ($i = 1; $i -le $steps; $i++) { Move-To ($x1 + ($x2 - $x1) * $i / $steps) ($y1 + ($y2 - $y1) * $i / $steps); Start-Sleep -Milliseconds $ms }
    Start-Sleep -Milliseconds 200
    [Desk]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero)
}
function Wheel($x, $y, $clicks) { Move-To $x $y; Start-Sleep -Milliseconds 100; [Desk]::mouse_event(0x800, 0, 0, 120 * $clicks, [IntPtr]::Zero) }
# Press "ctrl+shift+s", "space", "f4"...: modifiers held around the last key.
function Press($chord, $ms = 80, $count = 1) {
    $parts = @("$chord".Split('+') | ForEach-Object { Vk $_ })
    for ($n = 0; $n -lt $count; $n++) {
        foreach ($vk in $parts) { [Desk]::Key($vk, $false); Start-Sleep -Milliseconds 30 }
        Start-Sleep -Milliseconds $ms
        [array]::Reverse($parts); foreach ($vk in $parts) { [Desk]::Key($vk, $true); Start-Sleep -Milliseconds 30 }; [array]::Reverse($parts)
        Start-Sleep -Milliseconds 150
    }
}
# Text typed character by character (SendKeys, its specials escaped).
function Type-Text($text) {
    foreach ($c in "$text".ToCharArray()) {
        [System.Windows.Forms.SendKeys]::SendWait($(if ("+^%~(){}[]".Contains($c)) { "{$c}" } else { "$c" })); Start-Sleep -Milliseconds 30
    }
}
function Windows { [Desk]::Windows() }
function Focus($name) {
    $p = Get-Process $name -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { return "no window for $name" }
    $h = $p.MainWindowHandle
    for ($i = 0; $i -lt 5; $i++) {
        [Desk]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [Desk]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [Desk]::ShowWindow($h, 5) | Out-Null; [Desk]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([Desk]::GetForegroundWindow() -eq $h) { return "focused $name" }
    }
    "could not focus $name"
}
# The screen (or a rectangle of it) as a PNG, scaled by -Scale.
function Shot($path, $x = 0, $y = 0, $w = 0, $h = 0, $scale = 1.0) {
    $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    if ($w -le 0) { $x = 0; $y = 0; $w = $screen.Width; $h = $screen.Height }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen((New-Object System.Drawing.Point $x, $y), [System.Drawing.Point]::Empty, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    if ($scale -ne 1.0) {
        $small = New-Object System.Drawing.Bitmap ([int]($w * $scale)), ([int]($h * $scale))
        $g = [System.Drawing.Graphics]::FromImage($small)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.DrawImage($bmp, 0, 0, $small.Width, $small.Height); $g.Dispose(); $bmp.Dispose(); $bmp = $small
    }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "screen $($screen.Width)x$($screen.Height), shot ${x},${y} ${w}x${h} -> $path"
}

# Lua typed into DaVinci Resolve's console (F6; a window of Resolve's titled
# "Resolve") and run; the console closed again unless -Keep.
function Resolve-Lua($code, $keep = $false) {
    $resolve = Get-Process Resolve -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $resolve) { return "Resolve has no window" }
    $console = [Desk]::Find([uint32]$resolve.Id, 'Resolve')
    if (-not $console) {
        Focus Resolve | Out-Null
        Press f6
        for ($i = 0; $i -lt 20 -and -not $console; $i++) { Start-Sleep -Milliseconds 250; $console = [Desk]::Find([uint32]$resolve.Id, 'Resolve') }
        if (-not $console) { return "the console did not open" }
    }
    Click ([int](($console[1] + $console[3]) / 2)) ([int]($console[4] - 22))
    Type-Text $code
    Press enter
    if (-not $keep) {
        Start-Sleep -Milliseconds 500
        [Desk]::PostMessage([IntPtr]$console[0], 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    }
    "typed into Resolve's console at $($console[1]),$($console[2])-$($console[3]),$($console[4])"
}

New-Item -ItemType Directory -Force -Path $Dir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue "$Dir\stop", "$Dir\*.ps1", "$Dir\*.txt", "$Dir\*.out", "$Dir\*.tmp"
$identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
$elevated = $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
[IO.File]::WriteAllText("$Dir\server.ready", "pid $PID elevated $elevated session $((Get-Process -Id $PID).SessionId)")
while (-not (Test-Path "$Dir\stop")) {
    foreach ($file in @(Get-ChildItem -Path $Dir -Filter '*.ps1' | Sort-Object Name)) {
        $script = [IO.File]::ReadAllText($file.FullName)
        Remove-Item -Force -LiteralPath $file.FullName
        try { $out = (Invoke-Expression $script *>&1 | Out-String -Width 300) } catch { $out = "ERROR: $($_.ToString())" }
        [IO.File]::WriteAllText("$Dir\$($file.BaseName).tmp", "$out")
        Move-Item -Force -LiteralPath "$Dir\$($file.BaseName).tmp" -Destination "$Dir\$($file.BaseName).out"
    }
    Start-Sleep -Milliseconds 100
}
Remove-Item -Force -ErrorAction SilentlyContinue "$Dir\server.ready", "$Dir\stop"
