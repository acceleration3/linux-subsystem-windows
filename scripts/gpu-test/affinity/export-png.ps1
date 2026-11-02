Ready
Remove-Item -ErrorAction SilentlyContinue C:\av\affinity\export.png
Click 2060 394 3000
Press "ctrl+a" 300
TypeText 'C:\av\affinity\export.png' 500
Press enter 8000
"exported: $(Test-Path C:\av\affinity\export.png) $((Get-Item C:\av\affinity\export.png -ErrorAction SilentlyContinue).Length)"
(New-Object -ComObject Shell.Application).Windows() | ? { $_.LocationURL -like '*av/affinity*' } | % { $_.Quit() }
QuitNoSave
