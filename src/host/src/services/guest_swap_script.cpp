#include "lsw/host/services/guest_swap_script.hpp"

#include <string_view>

namespace lsw::host::services {

namespace {

// Runs inside the guest, started by the agent's own run_script — as
// SYSTEM in session 0, since the agent is the ``lsw-agent`` service.
//
// It stops that service, which ends the agent process. The script lives
// on regardless: run_script's job object has no KILL_ON_JOB_CLOSE (see
// install::ProcessRunner), and a service's children are not ended with
// it. ``taskkill`` then clears whatever still maps the old binary — the
// session helpers, which the service normally ends itself — because a
// mapped image cannot be overwritten.
//
// Delays use ping: ``timeout`` wants a console input it does not have.
constexpr std::string_view kHead = R"BAT(@echo off
setlocal
set DIR=C:\ProgramData\LSW
set EXE=%DIR%\lsw-agent.exe
set NEW=%DIR%\lsw-agent.new.exe
set LOG=%DIR%\lsw-swap.log
> "%LOG%" echo [swap] start %DATE% %TIME% as %USERNAME%
curl.exe --fail -L -o "%NEW%" http://10.0.2.2:__PORT__/lsw-guest-agent.exe >> "%LOG%" 2>&1
if errorlevel 1 ( echo [swap] download failed >> "%LOG%" & exit /b 1 )
for %%I in ("%NEW%") do echo [swap] downloaded size=%%~zI >> "%LOG%"
)BAT";

// Before the agent is stopped, and whatever becomes of the download:
// the mirror's guest half has nothing to do with the service, and a
// failure here leaves the old one where it was.
constexpr std::string_view kMirror = R"BAT(set RW=%DIR%\remotewin-guest.exe
set RWNEW=%DIR%\remotewin-guest.new.exe
curl.exe --fail -L -o "%RWNEW%" http://10.0.2.2:__PORT__/remotewin-guest.exe >> "%LOG%" 2>&1
if errorlevel 1 ( echo [swap] mirror download failed, keeping the old one >> "%LOG%" & goto mirror_done )
for %%I in ("%RWNEW%") do echo [swap] mirror downloaded size=%%~zI >> "%LOG%"
del /Q "%DIR%\remotewin-guest.*.old.exe" >nul 2>&1
if exist "%RW%" move /Y "%RW%" "%DIR%\remotewin-guest.%RANDOM%%RANDOM%.old.exe" >> "%LOG%" 2>&1
move /Y "%RWNEW%" "%RW%" >> "%LOG%" 2>&1
if errorlevel 1 ( echo [swap] mirror move failed >> "%LOG%" ) else ( echo [swap] mirror swapped >> "%LOG%" )
:mirror_done
)BAT";

constexpr std::string_view kAgent = R"BAT(ping -n 2 127.0.0.1 >nul
echo [swap] stopping the service >> "%LOG%"
sc.exe stop lsw-agent >> "%LOG%" 2>&1
set /a N=0
:wait_stopped
sc.exe query lsw-agent | find "STOPPED" >nul && goto stopped
set /a N+=1
if %N% geq 30 ( echo [swap] still not stopped after 30 s >> "%LOG%" & goto stopped )
ping -n 2 127.0.0.1 >nul
goto wait_stopped
:stopped
echo [swap] ending anything still running the old binary >> "%LOG%"
taskkill /F /IM lsw-agent.exe >> "%LOG%" 2>&1
ping -n 3 127.0.0.1 >nul
echo [swap] moving binary >> "%LOG%"
move /Y "%NEW%" "%EXE%" >> "%LOG%" 2>&1
if errorlevel 1 ( echo [swap] move failed, retrying >> "%LOG%" & ping -n 4 127.0.0.1 >nul & move /Y "%NEW%" "%EXE%" >> "%LOG%" 2>&1 )
echo [swap] starting the service >> "%LOG%"
sc.exe start lsw-agent >> "%LOG%" 2>&1
echo [swap] done >> "%LOG%"
exit /b 0
)BAT";

}  // namespace

std::string GuestSwapScript::text() const {
    std::string script{kHead};
    if (with_mirror_) {
        script += kMirror;
    }
    script += kAgent;
    const std::string needle{"__PORT__"};
    const std::string port = std::to_string(port_);
    for (auto pos = script.find(needle); pos != std::string::npos;
         pos = script.find(needle, pos + port.size())) {
        script.replace(pos, needle.size(), port);
    }
    return script;
}

}  // namespace lsw::host::services
