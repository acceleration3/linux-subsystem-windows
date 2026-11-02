Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Runtime.InteropServices;
public static class Shot {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  public struct RECT { public int L, T, R, B; }
  public static IntPtr Largest(uint[] pids) {
    IntPtr best = IntPtr.Zero; long area = 0;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      if (Array.IndexOf(pids, p) >= 0 && IsWindowVisible(h)) { RECT r; GetWindowRect(h, out r);
        long a = (long)(r.R - r.L) * (r.B - r.T); if (a > area) { area = a; best = h; } } return true; }, IntPtr.Zero);
    return best;
  }
  public static double Lit(System.Drawing.Bitmap b, int top, int bottom) {
    long lit = 0, all = 0;
    for (int y = Math.Max(0, top); y < Math.Min(b.Height, bottom); y += 4)
      for (int x = 0; x < b.Width; x += 4) { var c = b.GetPixel(x, y); all++; if (c.R + c.G + c.B > 24) lit++; }
    return all == 0 ? 0 : (double)lit / all;
  }
  public static double Brightness(System.Drawing.Bitmap b, int top, int bottom) {
    double sum = 0; long all = 0;
    for (int y = Math.Max(0, top); y < Math.Min(b.Height, bottom); y += 2)
      for (int x = 0; x < b.Width; x += 8) { var c = b.GetPixel(x, y); sum += (c.R + c.G + c.B) / 3.0; all++; }
    return all == 0 ? 0 : sum / all;
  }
}
"@
[Shot]::SetProcessDPIAware() | Out-Null
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$rect = $screen
if ($args.Count -gt 1) {
  $pids = [uint32[]]@(Get-Process $args[1] -ErrorAction SilentlyContinue | % { $_.Id })
  $h = [Shot]::Largest($pids)
  if ($h -eq [IntPtr]::Zero) { "window none"; exit 2 }
  [Shot]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [Shot]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
  [Shot]::ShowWindow($h, 5) | Out-Null; [Shot]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 700
  $r = New-Object Shot+RECT; [Shot]::GetWindowRect($h, [ref]$r) | Out-Null
  $rect = [System.Drawing.Rectangle]::Intersect($screen, (New-Object System.Drawing.Rectangle $r.L, $r.T, ($r.R - $r.L), ($r.B - $r.T)))
}
$bmp = New-Object System.Drawing.Bitmap $rect.Width, $rect.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect.Location, [System.Drawing.Point]::Empty, $rect.Size)
$bmp.Save($args[0], [System.Drawing.Imaging.ImageFormat]::Png)
"rect $($rect.X),$($rect.Y),$($rect.Width),$($rect.Height)"
$invariant = [Globalization.CultureInfo]::InvariantCulture
"lit " + [Shot]::Lit($bmp, 0, $bmp.Height).ToString("F3", $invariant)
if ($args.Count -le 1) { "taskbar " + [Shot]::Brightness($bmp, $bmp.Height - 40, $bmp.Height).ToString("F1", $invariant) }
