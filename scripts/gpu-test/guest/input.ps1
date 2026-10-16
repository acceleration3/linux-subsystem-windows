# input.ps1 press VK|NAME [MS] [COUNT] | click X Y | dclick X Y | keys "SENDKEYS" | focus PROCESS | sendto PROCESS "SENDKEYS"
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class In {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  public static void Press(byte vk, int ms) {
    byte sc = (byte)MapVirtualKey(vk, 0); uint ext = (vk >= 0x21 && vk <= 0x2E) ? 1u : 0u;
    keybd_event(vk, sc, 8 | ext, IntPtr.Zero); System.Threading.Thread.Sleep(ms); keybd_event(vk, sc, 8 | 2 | ext, IntPtr.Zero);
  }
}
"@
[In]::SetProcessDPIAware() | Out-Null
function Focus($name) {
  $p = Get-Process $name -ErrorAction SilentlyContinue | ? { $_.MainWindowHandle -ne 0 } | select -first 1
  if (-not $p) { "no window for $name"; return $false }
  $h = $p.MainWindowHandle
  for ($i = 0; $i -lt 5; $i++) {
    [In]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [In]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
    [In]::ShowWindow($h, 5) | Out-Null; [In]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 300
    if ([In]::GetForegroundWindow() -eq $h) { "focused $name"; return $true }
  }
  "could not focus $name"; return $false
}
$names = @{ enter=0x0D; esc=0x1B; space=0x20; left=0x25; up=0x26; right=0x27; down=0x28; tab=0x09; backspace=0x08 }
switch ($args[0]) {
  "press"  { $k = "$($args[1])".ToLower(); $vk = if ($names.ContainsKey($k)) { $names[$k] } elseif ($k.Length -eq 1) { [int][char]$k.ToUpper() } else { [int]$k }
             $ms = if ($args.Count -gt 2) { [int]$args[2] } else { 120 }; $n = if ($args.Count -gt 3) { [int]$args[3] } else { 1 }
             for ($i = 0; $i -lt $n; $i++) { [In]::Press([byte]$vk, $ms); Start-Sleep -Milliseconds 250 } }
  "click"  { [In]::SetCursorPos([int]$args[1], [int]$args[2]); Start-Sleep -Milliseconds 150; [In]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60; [In]::mouse_event(4,0,0,0,[IntPtr]::Zero) }
  "dclick" { [In]::SetCursorPos([int]$args[1], [int]$args[2]); 1..2 | % { [In]::mouse_event(2,0,0,0,[IntPtr]::Zero); [In]::mouse_event(4,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 80 } }
  "keys"   { [System.Windows.Forms.SendKeys]::SendWait(($args[1..($args.Count-1)] -join " ")) }
  "focus"  { Focus $args[1] | Out-Null }
  "sendto" { if (Focus $args[1]) { foreach ($c in ($args[2..($args.Count-1)] -join " ").ToCharArray()) { [System.Windows.Forms.SendKeys]::SendWait($(if ("+^%~(){}[]".Contains($c)) { "{$c}" } else { "$c" })); Start-Sleep -Milliseconds 40 }; [System.Windows.Forms.SendKeys]::SendWait("{ENTER}") } }
}
"done $($args -join ' ')"
