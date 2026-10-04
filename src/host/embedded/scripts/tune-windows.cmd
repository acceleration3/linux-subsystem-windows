@echo off
setlocal

rem ---------------------------------------------------------------------------
rem Tune the Windows guest for headless VM use.
rem
rem Embedded into the manager binary at build time and dispatched by the
rem wizard's SetupPage as a ``run_script`` step. Referenced from
rem setup-steps.json via ``"script_ref": "tune-windows"``. To edit, change
rem this file and rebuild — no manual JSON escaping.
rem
rem Runs as SYSTEM in session 0: the agent is a service, and setup steps
rem run there unless they ask for a session. So everything here is
rem machine-wide (HKLM, powercfg) — an HKCU write would land in SYSTEM's
rem own hive, not the user's. Per-user defaults belong in the
rem autounattend's DefaultUser.ps1, which writes the default profile
rem before the wizard user exists.
rem ---------------------------------------------------------------------------

rem Disable hibernation — no point in a VM and saves the hiberfil.sys disk.
powercfg /h off >nul 2>&1

rem Activate the Ultimate Performance scheme when available (Win10 1803+
rem Pro/Enterprise). powercfg /duplicatescheme creates a copy with a new
rem GUID; we capture it via a temp file. On Home editions the duplicate
rem fails and we fall back to High Performance, which still uncaps the
rem CPU but keeps a few timers.
powercfg /duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61 > "%TEMP%\lsw-pp.txt" 2>nul
set ULTGUID=
for /f "tokens=4 delims= " %%G in ('findstr /c:"GUID:" "%TEMP%\lsw-pp.txt" 2^>nul') do set ULTGUID=%%G
del "%TEMP%\lsw-pp.txt" >nul 2>&1
if defined ULTGUID (
    powercfg /setactive %ULTGUID% >nul 2>&1
) else (
    powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c >nul 2>&1
)

rem Zero every -ac idle timer. A VM never sees battery, so the -dc side
rem doesn't matter.
powercfg -change -standby-timeout-ac   0 >nul 2>&1
powercfg -change -monitor-timeout-ac   0 >nul 2>&1
powercfg -change -disk-timeout-ac      0 >nul 2>&1
powercfg -change -hibernate-timeout-ac 0 >nul 2>&1

rem Disable the lock screen so launch RPCs always hit an interactive session.
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows\Personalization" /v NoLockScreen /t REG_DWORD /d 1 /f >nul 2>&1

rem Disable Win+L / policy-driven workstation lock.
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" /v DisableLockWorkstation /t REG_DWORD /d 1 /f >nul 2>&1

rem Screen-saver off and no password on resume: per-user, so it is set in
rem the default profile by the autounattend's DefaultUser.ps1, not here.

rem Disable Windows Update auto-reboot while a user is logged in.
reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU" /v NoAutoRebootWithLoggedOnUsers /t REG_DWORD /d 1 /f >nul 2>&1

exit /b 0
