ToMenu
Press enter 3000
Press enter 3000
$until = (Get-Date).AddSeconds(400)
$lit = 0
while ((Get-Date) -lt $until) {
  $b = Client
  if ($b) { try { if ((Mean $b 0.1 0.1 0.8 0.8) -gt 0.04) { $lit++ } else { $lit = 0 } } finally { $b.Dispose() } }
  if ($lit -ge 3) { break }
  Start-Sleep 2
}
Log "the cutscene shows: $($lit -ge 3)"
Press f12 200
Start-Sleep 90
Quit
