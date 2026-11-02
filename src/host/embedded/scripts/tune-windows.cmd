@echo off
setlocal

powercfg /h off >nul 2>&1

powercfg /duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61 > "%TEMP%\lsw-pp.txt" 2>nul
set ULTGUID=
for /f "tokens=4 delims= " %%G in ('findstr /c:"GUID:" "%TEMP%\lsw-pp.txt" 2^>nul') do set ULTGUID=%%G
del "%TEMP%\lsw-pp.txt" >nul 2>&1
if defined ULTGUID (
    powercfg /setactive %ULTGUID% >nul 2>&1
) else (
    powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c >nul 2>&1
)

powercfg -change -standby-timeout-ac   0 >nul 2>&1
powercfg -change -monitor-timeout-ac   0 >nul 2>&1
powercfg -change -disk-timeout-ac      0 >nul 2>&1
powercfg -change -hibernate-timeout-ac 0 >nul 2>&1

reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows\Personalization" /v NoLockScreen /t REG_DWORD /d 1 /f >nul 2>&1

reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" /v DisableLockWorkstation /t REG_DWORD /d 1 /f >nul 2>&1

reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU" /v NoAutoRebootWithLoggedOnUsers /t REG_DWORD /d 1 /f >nul 2>&1

exit /b 0
