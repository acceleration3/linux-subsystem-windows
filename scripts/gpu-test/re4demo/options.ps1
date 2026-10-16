# re4demo/options: from the title to the main menu, OPTIONS opened, then
# its tabs gone through with E - Controls, Camera (with its preview
# picture), Display, Graphics, Language, Audio - nothing changed; recorded
# from the F12 pressed on the Controls tab for 240 frames.
ToMenu
Press down 1500
Press enter 4000
Press f12 200
Start-Sleep 4
foreach ($tab in 1..5) { Press e 5000 }
Start-Sleep 30
Quit
