' hidden.vbs SCRIPT.cmd : runs the script with no window, exits with its code
Set shell = CreateObject("WScript.Shell")
WScript.Quit shell.Run("cmd.exe /c """ & WScript.Arguments(0) & """", 0, True)
