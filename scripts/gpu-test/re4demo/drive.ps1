# drive.ps1: scripted keyboard and mouse for the Resident Evil 4 Chainsaw Demo in the
# desktop, put before each scene's script by traces.py capture --drive.
#
# The game reads the keyboard by scan code and the camera from relative mouse
# motion (raw input), so keys are sent as scan codes and the mouse as
# relative moves. DSR's keyboard defaults: W A S D move, mouse turns the
# camera, E confirms and interacts, Q backs out, Esc opens the menu, left
# click attacks, Space rolls, Tab locks on.
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class G {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, int d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  public static void Key(byte vk, bool up) {
    byte sc = (byte)MapVirtualKey(vk, 0); uint ext = (vk >= 0x21 && vk <= 0x2E) ? 1u : 0u;
    keybd_event(vk, sc, 8u | ext | (up ? 2u : 0u), IntPtr.Zero);
  }
}
"@
[G]::SetProcessDPIAware() | Out-Null
$GameProcess = 're4demo'
$VK = @{ enter=0x0D; esc=0x1B; space=0x20; tab=0x09; shift=0x10; ctrl=0x11; alt=0x12; backspace=0x08;
         left=0x25; up=0x26; right=0x27; down=0x28; f12=0x7B }
function VkOf($k) { $k = "$k".ToLower(); if ($VK.ContainsKey($k)) { $VK[$k] } elseif ($k.Length -eq 1) { [int][char]$k.ToUpper() } else { [int]$k } }
function Log($line) { "[$((Get-Date).ToString('HH:mm:ss.fff'))] $line" }
function GameWindow { Get-Process $GameProcess -ErrorAction SilentlyContinue | ? { $_.MainWindowHandle -ne 0 } | select -First 1 }
function Focus {
  $p = GameWindow; if (-not $p) { throw "no $GameProcess window" }
  for ($i = 0; $i -lt 6; $i++) {
    if ([G]::GetForegroundWindow() -eq $p.MainWindowHandle) { return }
    [G]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [G]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
    [G]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 300
  }
  Log "could not focus $GameProcess"
}
# A key tapped (held `hold` ms), then `wait` ms.
function Press($key, $wait = 600, $hold = 120) {
  $k = [byte](VkOf $key); [G]::Key($k, $false); Start-Sleep -Milliseconds $hold; [G]::Key($k, $true); Start-Sleep -Milliseconds $wait
}
# Keys held together for `ms` (walking: Hold w 2000; running: Hold @('w','space') 2000).
function Hold($keys, $ms) {
  $codes = @($keys | % { [byte](VkOf $_) })
  foreach ($k in $codes) { [G]::Key($k, $false) }
  Start-Sleep -Milliseconds $ms
  foreach ($k in $codes) { [G]::Key($k, $true) }
  Start-Sleep -Milliseconds 150
}
# The camera turned: relative mouse motion of (dx, dy) in `steps` moves over `ms`.
function Turn($dx, $dy, $ms = 1000, $steps = 50) {
  for ($i = 0; $i -lt $steps; $i++) {
    [G]::mouse_event(1, [int]($dx / $steps), [int]($dy / $steps), 0, [IntPtr]::Zero); Start-Sleep -Milliseconds ([int]($ms / $steps))
  }
  Start-Sleep -Milliseconds 150
}
# Left click (the right hand's attack), right (block) with -Right.
function Attack($wait = 900, [switch]$Right) {
  $down, $up = if ($Right) { 8, 16 } else { 2, 4 }
  [G]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80
  [G]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds $wait
}
function WaitGame($seconds = 120) {
  $until = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $until) { if (GameWindow) { return }; Start-Sleep -Milliseconds 500 }
  throw "$GameProcess showed no window"
}
# The game window's client area as a bitmap (1280x720), to tell where the
# game is: the title menu, a notice, or the game with its HUD.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Wr { public struct RECT { public int L, T, R, B; } public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p); }
"@
function Client {
  $p = GameWindow; if (-not $p) { return $null }
  $r = New-Object Wr+RECT; [Wr]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
  $pt = New-Object Wr+POINT; [Wr]::ClientToScreen($p.MainWindowHandle, [ref]$pt) | Out-Null
  if ($r.R -le 0) { return $null }
  $bmp = New-Object System.Drawing.Bitmap $r.R, $r.B
  $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, $bmp.Size); $g.Dispose()
  $bmp
}
# The game's client area saved as a PNG (for finding where the game is).
function Snap($path) { $b = Client; if ($b) { $b.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose() } }
# The game closed (WM_CLOSE, as trace-capture.ps1's -Seconds does) and gone.
function Quit {
  Get-Process $GameProcess -ErrorAction SilentlyContinue | % { [void]$_.CloseMainWindow() }
  $until = (Get-Date).AddSeconds(60)
  while ((Get-Process $GameProcess -ErrorAction SilentlyContinue) -and (Get-Date) -lt $until) { Start-Sleep 1 }
  Log "$GameProcess exited: $(-not (Get-Process $GameProcess -ErrorAction SilentlyContinue))"
}
# The mean brightness (0-1) of a part of the client area, in fractions of
# its size, sampled every 4th pixel.
function Mean($bmp, $fx, $fy, $fw, $fh) {
  $x0 = [int]($fx * $bmp.Width); $y0 = [int]($fy * $bmp.Height)
  $x1 = [int](($fx + $fw) * $bmp.Width); $y1 = [int](($fy + $fh) * $bmp.Height)
  $sum = 0.0; $n = 0
  for ($y = $y0; $y -lt $y1; $y += 4) { for ($x = $x0; $x -lt $x1; $x += 4) { $c = $bmp.GetPixel($x, $y); $sum += $c.R + $c.G + $c.B; $n++ } }
  if ($n -eq 0) { 0 } else { $sum / $n / 765.0 }
}
# Where the game is, by looking: the title (its big logo), a menu (an entry
# at the top left: MAIN STORY, NEW GAME), or neither (logos, loading).
function Screen {
  $b = Client; if (-not $b) { return 'none' }
  try {
    if ((Mean $b 0.30 0.27 0.40 0.20) -gt 0.25) { return 'title' }
    if ((Mean $b 0.06 0.21 0.08 0.03) -gt 0.08) { return 'menu' }
    'other'
  } finally { $b.Dispose() }
}
function WaitScreen($want, $seconds = 300) {
  $until = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $until) { if ((Screen) -eq $want) { Log "at the $want"; return }; Start-Sleep 2 }
  throw "the game never showed its $want"
}
# From the launch to the title: the logos and the development notice pass by
# themselves (Enter on them would skip ahead unevenly).
function ToTitle { WaitGame; WaitScreen title 400; Start-Sleep 3; Focus }
# From the title to the main menu (MAIN STORY, OPTIONS, ONLINE STORE, EXIT).
function ToMenu { ToTitle; Press enter 1000; WaitScreen menu 60; Start-Sleep 2 }
