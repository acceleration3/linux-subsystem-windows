# re4demo/options: from the title to the main menu, OPTIONS opened on its
# Controls tab, then recorded from F12 for 40 frames while E is held for each
# of the next tabs - Camera (with its preview picture), Display, Graphics,
# Language - nothing changed. Under the capture the menus run at a frame
# every few seconds, so each key is held across a frame or two rather than
# tapped (a tap between two frames is never seen).
ToMenu
Hold down 1500; Start-Sleep 3
Hold enter 1500; Start-Sleep 8
Press f12 300
Start-Sleep 20
foreach ($tab in 1..4) { Hold e 6000; Start-Sleep 20 }
Start-Sleep 30
Quit
