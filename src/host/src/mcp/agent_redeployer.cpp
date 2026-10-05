#include "lsw/host/mcp/agent_redeployer.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mcp/guest_file_server.hpp"
#include "lsw/host/mcp/guest_swap_script.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"

#include <QFile>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

namespace {

constexpr auto kPollWindow = std::chrono::seconds(45);
constexpr auto kPollInterval = std::chrono::seconds(1);

// Where the swap script puts the mirror's guest half.
constexpr const char* kGuestDir = "C:\\ProgramData\\LSW";

// A file's bytes, or why there are none.
[[nodiscard]] QByteArray read_whole(const QString& path, std::string* error) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        *error = "cannot open " + path.toStdString() + ": " + file.errorString().toStdString();
        return {};
    }
    QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        *error = path.toStdString() + " is empty";
    }
    return bytes;
}

}  // namespace

AgentRedeployer::Outcome AgentRedeployer::run() {
    const QString exe_path = platform::AgentExePath::locate();
    if (exe_path.isEmpty()) {
        return {false, "redeploy_agent: cannot find lsw-guest-agent.exe; "
                       "set $LSW_AGENT_EXE or run scripts/build-agent.sh"};
    }
    std::string read_error;
    const QByteArray exe_bytes = read_whole(exe_path, &read_error);
    if (exe_bytes.isEmpty()) {
        return {false, "redeploy_agent: " + read_error};
    }

    // The mirror's guest half goes with the agent when there is one to
    // send; without it the guest keeps the one it has, and says so.
    GuestFileServer::Files files{{GuestSwapScript::kAgentFile, exe_bytes}};
    const QString mirror_path = platform::MirrorExePaths::guest();
    QByteArray mirror_bytes;
    std::string mirror_note;
    if (mirror_path.isEmpty()) {
        mirror_note = "remotewin-guest.exe not found here (set $LSW_REMOTEWIN_GUEST or run "
                      "scripts/build-agent.sh); the guest's was left as it is";
    } else {
        mirror_bytes = read_whole(mirror_path, &read_error);
        if (mirror_bytes.isEmpty()) {
            mirror_note = "remotewin-guest.exe: " + read_error + "; the guest's was left as it is";
        } else {
            files.emplace(GuestSwapScript::kMirrorFile, mirror_bytes);
        }
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

    GuestFileServer binary_server{std::move(files)};
    if (!binary_server.start()) {
        return {false, "redeploy_agent: " + binary_server.error()};
    }

    const auto ack = agent_.request(linkproto::cmd::kRunScript, {
        {"name", "redeploy agent"},
        {"script", GuestSwapScript{binary_server.port(), !mirror_bytes.isEmpty()}.text()},
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
    if (!mirror_bytes.isEmpty()) {
        mirror_note = check_mirror(mirror_bytes.size());
    }
    return {true, "redeploy_agent: success, agent_version '" + previous_version + "' -> '"
                  + new_version + "' (" + served + " download(s) served on port " + port + "). "
                  + mirror_note};
}

std::string AgentRedeployer::check_mirror(qsizetype served_size) {
    const auto listing = agent_.request(linkproto::cmd::kListDir,
                                        {{"path", kGuestDir}, {"suffix", ".exe"}});
    if (!listing.ok) {
        return "remotewin-guest.exe: could not list the guest's folder to check it: "
               + listing.error;
    }
    for (const auto& entry : listing.body.value("entries", nlohmann::json::array())) {
        if (entry.value("name", std::string{}) != GuestSwapScript::kMirrorFile) {
            continue;
        }
        const auto size = entry.value("size", std::int64_t{-1});
        if (size == served_size) {
            return "remotewin-guest.exe swapped (" + std::to_string(size)
                   + " bytes); mirrors started from now on run it, running ones keep the old one";
        }
        return "remotewin-guest.exe NOT swapped: the guest's is " + std::to_string(size)
               + " bytes, the one served " + std::to_string(served_size)
               + ". Check C:\\ProgramData\\LSW\\lsw-swap.log on the guest";
    }
    return "remotewin-guest.exe is not in the guest's C:\\ProgramData\\LSW after the swap. "
           "Check lsw-swap.log there";
}

}  // namespace lsw::host::mcp
