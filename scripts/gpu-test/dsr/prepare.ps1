# prepare.ps1: run in the VM's desktop before each DSR capture. The saved
# game every scene starts from (the Undead Asylum courtyard, at the foot of
# the stairs to the big door, the bonfire to the right, 1280x720 windowed,
# offline), kept in C:\av\dsr, is put back, and other windows are minimized
# so the game's window is in front.
$save = 'C:\Users\lsw\Documents\NBGI\DARK SOULS REMASTERED\41380770\DRAKS0005.sl2'
if (Get-Process DarkSoulsRemastered -ErrorAction SilentlyContinue) { throw "Dark Souls Remastered is running" }
Copy-Item -Force C:\av\dsr\DRAKS0005.sl2 $save
Copy-Item -Force C:\av\dsr\DarkSouls.ini "$env:LOCALAPPDATA\FromSoftware\NBGI\DarkSouls\DarkSouls.ini"
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Mn { [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c); }
"@
Get-Process | ? { $_.MainWindowHandle -ne 0 -and $_.ProcessName -ne 'DarkSoulsRemastered' } | % { [void][Mn]::ShowWindow($_.MainWindowHandle, 6) }
"save restored: $((Get-FileHash $save).Hash)"
