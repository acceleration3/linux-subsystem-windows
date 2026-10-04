#include "lsw/host/mcp/agent_redeployer.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/agent_binary_server.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"

#include <QFile>

#include <chrono>
#include <thread>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

namespace {

// Runs inside the guest, started by the agent's own run_script — as
// SYSTEM in session 0, since the agent is the ``lsw-agent`` service.
//
// It stops that service, which ends the agent process. The script lives
// on regardless: run_script's job object has no KILL_ON_JOB_CLOSE (see
// install::ProcessRunner), and a service's children are not ended with
// it. ``taskkill`` then clears whatever still maps the old binary — the
// session helpers, which the service normally ends itself — because a
// mapped image cannot be overwritten. Then the swap, and the service is
// started again; it is started even when the move failed, so a failed
// swap still leaves the old agent running.
//
// Delays use ping: ``timeout`` wants a console input it does not have.
constexpr const char* kSwapScript = R"BAT(@echo off
setlocal
set DIR=C:\ProgramData\LSW
set EXE=%DIR%\lsw-agent.exe
set NEW=%DIR%\lsw-agent.new.exe
set LOG=%DIR%\lsw-swap.log
> "%LOG%" echo [swap] start %DATE% %TIME% as %USERNAME%
curl.exe --fail -L -o "%NEW%" http://10.0.2.2:__PORT__/lsw-guest-agent.exe >> "%LOG%" 2>&1
if errorlevel 1 ( echo [swap] download failed >> "%LOG%" & exit /b 1 )
for %%I in ("%NEW%") do echo [swap] downloaded size=%%~zI >> "%LOG%"
ping -n 2 127.0.0.1 >nul
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

[[nodiscard]] std::string swap_script_for(unsigned short port) {
    std::string script = kSwapScript;
    const std::string needle{"__PORT__"};
    if (const auto pos = script.find(needle); pos != std::string::npos) {
        script.replace(pos, needle.size(), std::to_string(port));
    }
    return script;
}

constexpr auto kPollWindow = std::chrono::seconds(45);
constexpr auto kPollInterval = std::chrono::seconds(1);

}  // namespace

AgentRedeployer::Outcome AgentRedeployer::run() {
    const QString exe_path = platform::AgentExePath::locate();
    if (exe_path.isEmpty()) {
        return {false, "redeploy_agent: cannot find lsw-guest-agent.exe; "
                       "set $LSW_AGENT_EXE or run scripts/build-agent.sh"};
    }
    QFile file{exe_path};
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, "redeploy_agent: cannot open " + exe_path.toStdString() + ": "
                       + file.errorString().toStdString()};
    }
    const QByteArray exe_bytes = file.readAll();
    file.close();
    if (exe_bytes.isEmpty()) {
        return {false, "redeploy_agent: " + exe_path.toStdString() + " is empty"};
    }

    // Baseline version: the swap is done once a different one answers.
    const auto before = agent_.request(linkproto::cmd::kSystemInfo);
    if (!before.ok) {
        return {false, "redeploy_agent: agent unreachable before swap: " + before.error};
    }
    const std::string previous_version = before.body.value("agent_version", std::string{});
    if (previous_version.empty()) {
        return {false, "redeploy_agent: the running agent's system_info has no agent_version"};
    }

    AgentBinaryServer binary_server{exe_bytes};
    if (!binary_server.start()) {
        return {false, "redeploy_agent: " + binary_server.error()};
    }

    const auto ack = agent_.request(linkproto::cmd::kRunScript, {
        {"name", "redeploy agent"},
        {"script", swap_script_for(binary_server.port())},
    });
    if (!ack.ok) {
        return {false, "redeploy_agent: run_script RPC failed: " + ack.error};
    }

    // Poll until a *different* binary answers. The channel reconnects on
    // a 500 ms backoff after the script stops the old agent; the new one
    // listens once its session helper has confirmed the desktop, a few
    // seconds after the service starts.
    const auto deadline = std::chrono::steady_clock::now() + kPollWindow;
    std::string new_version;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPollInterval);
        const auto check = agent_.request(linkproto::cmd::kSystemInfo);
        if (!check.ok) {
            continue;
        }
        const auto v = check.body.value("agent_version", std::string{});
        if (!v.empty() && v != previous_version) {
            new_version = v;
            break;
        }
    }

    const std::string served = std::to_string(binary_server.downloads_served());
    const std::string port = std::to_string(binary_server.port());
    if (new_version.empty()) {
        return {false, "redeploy_agent: timed out (45 s) waiting for new agent. Served "
                       + served + " download(s) on port " + port + ", prev_version='"
                       + previous_version
                       + "'. Check C:\\ProgramData\\LSW\\lsw-swap.log on the guest for details."};
    }
    return {true, "redeploy_agent: success, agent_version '" + previous_version + "' -> '"
                  + new_version + "' (" + served + " download(s) served on port " + port + ")"};
}

}  // namespace lsw::host::mcp
