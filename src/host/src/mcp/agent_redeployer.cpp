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

// Runs inside the guest. ``start ""`` detaches the relaunched agent so it
// survives both this cmd.exe and the agent process the script just
// killed. The script runs inside the agent's own (elevated, interactive)
// context, so a plain start relaunches it the same way — no scheduled
// task, no token juggling.
constexpr const char* kSwapScript = R"BAT(@echo off
setlocal
set NEW=%TEMP%\lsw-agent-new.exe
set LOG=%TEMP%\lsw-swap.log
> "%LOG%" echo [swap] start %DATE% %TIME%
curl --fail -L -o "%NEW%" http://10.0.2.2:__PORT__/lsw-guest-agent.exe >> "%LOG%" 2>&1
if errorlevel 1 ( echo [swap] download failed >> "%LOG%" & exit /b 1 )
for %%I in ("%NEW%") do echo [swap] downloaded size=%%~zI >> "%LOG%"
timeout /t 1 /nobreak >nul
echo [swap] killing agent... >> "%LOG%"
taskkill /F /IM lsw-agent.exe >> "%LOG%" 2>&1
timeout /t 3 /nobreak >nul
echo [swap] moving binary >> "%LOG%"
move /Y "%NEW%" "C:\ProgramData\LSW\lsw-agent.exe" >> "%LOG%" 2>&1
echo [swap] relaunching >> "%LOG%"
start "" "C:\ProgramData\LSW\lsw-agent.exe"
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
        return {false, "redeploy_agent: cannot find lsw-guest-agent.exe — "
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

    // Baseline version. Empty if the running agent predates the field —
    // in that case any non-empty value afterwards counts as a change.
    const auto before = agent_.request(linkproto::cmd::kSystemInfo);
    if (!before.ok) {
        return {false, "redeploy_agent: agent unreachable before swap: " + before.error};
    }
    const std::string previous_version = before.body.value("agent_version", std::string{});

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
    // a 500 ms backoff after the script kills the old agent, and the new
    // one usually listens within a second or two of relaunch.
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
                       + "'. Check %TEMP%\\lsw-swap.log on the guest for details."};
    }
    return {true, "redeploy_agent: success — agent_version '" + previous_version + "' -> '"
                  + new_version + "' (" + served + " download(s) served on port " + port + ")"};
}

}  // namespace lsw::host::mcp
