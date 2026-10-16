# Export PNG (the toolbar's quick export) of the unedited photo: saved as C:\av\affinity\export.png (deleted first, 10.9 MB written); Affinity then shows it in an Explorer window.
Ready
Remove-Item -ErrorAction SilentlyContinue C:\av\affinity\export.png
Click 2060 394 3000
Press "ctrl+a" 300
TypeText 'C:\av\affinity\export.png' 500
Press enter 8000
"exported: $(Test-Path C:\av\affinity\export.png) $((Get-Item C:\av\affinity\export.png -ErrorAction SilentlyContinue).Length)"
# Affinity shows the export in an Explorer window: closed.
(New-Object -ComObject Shell.Application).Windows() | ? { $_.LocationURL -like '*av/affinity*' } | % { $_.Quit() }
QuitNoSave
