Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class D {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, int d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
  public static void Key(byte vk, bool up) {
    byte sc = (byte)MapVirtualKey(vk, 0); uint ext = (vk >= 0x21 && vk <= 0x2E) ? 1u : 0u;
    keybd_event(vk, sc, ext | (up ? 2u : 0u), IntPtr.Zero);
  }
}
"@
[D]::SetProcessDPIAware() | Out-Null
$VK = @{ ctrl=0x11; shift=0x10; alt=0x12; enter=0x0D; esc=0x1B; space=0x20; tab=0x09; del=0x2E; backspace=0x08;
         left=0x25; up=0x26; right=0x27; down=0x28; plus=0xBB; minus=0xBD; "]"=0xDD; "["=0xDB; f4=0x73; home=0x24; end=0x23 }
function VkOf($k) { $k = "$k".ToLower(); if ($VK.ContainsKey($k)) { $VK[$k] } elseif ($k.Length -eq 1) { [int][char]$k.ToUpper() } else { [int]$k } }

function AffWindow { Get-Process Affinity -ErrorAction SilentlyContinue | ? { $_.MainWindowHandle -ne 0 } | select -First 1 }
function Focus {
  $p = AffWindow; if (-not $p) { throw "no Affinity window" }
  $h = $p.MainWindowHandle
  if ([D]::GetForegroundWindow() -eq $h) { return }
  for ($i = 0; $i -lt 6; $i++) {
    [D]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [D]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
    [D]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 300
    if ([D]::GetForegroundWindow() -eq $h) { Press esc 200; return }
  }
  Log "could not focus Affinity"
}
function Log($line) { "[$((Get-Date).ToString('HH:mm:ss.fff'))] $line" }
function Press($combo, $wait = 400) {
  $keys = @($combo -split '\+' | % { VkOf $_ })
  foreach ($k in $keys) { [D]::Key([byte]$k, $false); Start-Sleep -Milliseconds 30 }
  Start-Sleep -Milliseconds 60
  [array]::Reverse($keys); foreach ($k in $keys) { [D]::Key([byte]$k, $true); Start-Sleep -Milliseconds 30 }
  Start-Sleep -Milliseconds $wait
}
function TypeText($text, $wait = 300) {
  foreach ($c in $text.ToCharArray()) { [System.Windows.Forms.SendKeys]::SendWait($(if ("+^%~(){}[]".Contains($c)) { "{$c}" } else { "$c" })); Start-Sleep -Milliseconds 25 }
  Start-Sleep -Milliseconds $wait
}
function MoveTo($x, $y) { [D]::SetCursorPos([int]$x, [int]$y) | Out-Null }
function Click($x, $y, $wait = 400, [switch]$Right, [switch]$Double) {
  MoveTo $x $y; Start-Sleep -Milliseconds 150
  $down, $up = if ($Right) { 8, 16 } else { 2, 4 }
  $n = if ($Double) { 2 } else { 1 }
  for ($i = 0; $i -lt $n; $i++) { [D]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 50; [D]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 70 }
  Start-Sleep -Milliseconds $wait
}
function Drag([int[]]$pts, $steps = 12, $ms = 12, $wait = 400, [switch]$Middle) {
  $down, $up = if ($Middle) { 0x20, 0x40 } else { 2, 4 }
  MoveTo $pts[0] $pts[1]; Start-Sleep -Milliseconds 120
  [D]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
  for ($i = 2; $i -lt $pts.Count; $i += 2) {
    $x0, $y0, $x1, $y1 = $pts[$i - 2], $pts[$i - 1], $pts[$i], $pts[$i + 1]
    for ($s = 1; $s -le $steps; $s++) { MoveTo ($x0 + ($x1 - $x0) * $s / $steps) ($y0 + ($y1 - $y0) * $s / $steps); Start-Sleep -Milliseconds $ms }
  }
  Start-Sleep -Milliseconds 60
  [D]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds $wait
}
function Wheel($x, $y, $clicks, $mods = @(), $wait = 250) {
  MoveTo $x $y; Start-Sleep -Milliseconds 100
  foreach ($m in $mods) { [D]::Key([byte](VkOf $m), $false) }
  $d = if ($clicks -gt 0) { 120 } else { -120 }
  for ($i = 0; $i -lt [math]::Abs($clicks); $i++) { [D]::mouse_event(0x800, 0, 0, $d, [IntPtr]::Zero); Start-Sleep -Milliseconds $wait }
  foreach ($m in $mods) { [D]::Key([byte](VkOf $m), $true) }
}
function WaitWindow($seconds = 120) {
  $until = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $until) { $p = AffWindow; if ($p -and $p.MainWindowTitle) { return $p }; Start-Sleep -Milliseconds 500 }
  throw "Affinity showed no window"
}
function WinRect { $p = AffWindow; $r = New-Object D+RECT; [D]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null; $r }
function QuitNoSave {
  Focus; Press "alt+f4" 2500
  if (Get-Process Affinity -ErrorAction SilentlyContinue) { Click 1268 860 1000 }
  $until = (Get-Date).AddSeconds(90); while ((Get-Process Affinity -ErrorAction SilentlyContinue) -and (Get-Date) -lt $until) { Start-Sleep 1 }
  Log "affinity exited: $(-not (Get-Process Affinity -ErrorAction SilentlyContinue))"
}
function Ready { $p = WaitWindow 150; Start-Sleep 15; Focus; Press v 400; Log "ready" }
function Launch { Invoke-CommandInDesktopPackage -PackageFamilyName Canva.Affinity_8a0j1tnjnt4a4 -AppId Canva.Affinity -Command 'C:\Program Files\WindowsApps\Canva.Affinity_3.3.1.4940_x64__8a0j1tnjnt4a4\App\Affinity.exe' -Args 'C:\av\affinity\photo.jpg' }
