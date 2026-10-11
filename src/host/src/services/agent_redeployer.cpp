#include "lsw/host/services/agent_redeployer.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/services/guest_file_server.hpp"
#include "lsw/host/services/guest_swap_script.hpp"

#include <QCryptographicHash>
#include <QFile>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::services {

namespace {

constexpr auto kPollWindow = std::chrono::seconds(45);
constexpr auto kPollInterval = std::chrono::seconds(1);

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

// Which agent process answers: its build and when it started.
struct Answering {
    bool ok = false;
    std::string version;
    std::uint64_t started = 0;

    [[nodiscard]] bool same_process(const Answering& other) const {
        return version == other.version && started == other.started;
    }
};

[[nodiscard]] Answering answering(link::AgentRpcClient& agent) {
    const auto reply = agent.request(linkproto::cmd::kSystemInfo);
    if (!reply.ok) {
        return {};
    }
    return {true, reply.body.value("agent_version", std::string{}),
            reply.body.value("agent_started", std::uint64_t{0})};
}

}  // namespace

AgentRedeployer::Shipped AgentRedeployer::shipped() {
    Shipped out;
    const QString agent_path = platform::AgentExePath::locate();
    if (agent_path.isEmpty()) {
        out.error = "cannot find lsw-guest-agent.exe; set $LSW_AGENT_EXE or run "
                    "scripts/build-agent.sh";
        return out;
    }
    out.agent = read_whole(agent_path, &out.error);
    const QString mirror_path = platform::MirrorExePaths::guest();
    if (mirror_path.isEmpty()) {
        out.note = "remotewin-guest.exe not found here (set $LSW_REMOTEWIN_GUEST or run "
                   "scripts/build-agent.sh); the guest's was left as it is";
        return out;
    }
    std::string mirror_error;
    out.mirror = read_whole(mirror_path, &mirror_error);
    if (out.mirror.isEmpty()) {
        out.note = "remotewin-guest.exe: " + mirror_error + "; the guest's was left as it is";
    }
    return out;
}

std::string AgentRedeployer::sha256(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

AgentRedeployer::Outcome AgentRedeployer::run() {
    Shipped binaries = shipped();
    if (binaries.agent.isEmpty()) {
        return {false, "redeploy_agent: " + binaries.error};
    }
    const std::string agent_hash = sha256(binaries.agent);
    const std::string mirror_hash = binaries.mirror.isEmpty() ? std::string{}
                                                              : sha256(binaries.mirror);
    // The mirror's guest half goes with the agent when there is one to
    // send; without it the guest keeps the one it has, and says so.
    GuestFileServer::Files files{{GuestSwapScript::kAgentFile, binaries.agent}};
    if (!binaries.mirror.isEmpty()) {
        files.emplace(GuestSwapScript::kMirrorFile, binaries.mirror);
    }

    // Baseline: the swap is done once another process answers.
    const Answering before = answering(agent_);
    if (!before.ok) {
        return {false, "redeploy_agent: the agent is unreachable before the swap"};
    }

    GuestFileServer binary_server{std::move(files)};
    if (!binary_server.start()) {
        return {false, "redeploy_agent: " + binary_server.error()};
    }

    const auto ack = agent_.request(linkproto::cmd::kRunScript, {
        {"name", "redeploy agent"},
        {"script", GuestSwapScript{binary_server.port(), !binaries.mirror.isEmpty()}.text()},
    });
    if (!ack.ok) {
        return {false, "redeploy_agent: run_script RPC failed: " + ack.error};
    }

    // Poll until another process answers. The channel reconnects on a
    // 500 ms backoff after the script stops the old agent; the new one
    // listens once its session helper has confirmed the desktop, a few
    // seconds after the service starts.
    const auto deadline = std::chrono::steady_clock::now() + kPollWindow;
    Answering after;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPollInterval);
        const Answering now = answering(agent_);
        if (now.ok && !now.same_process(before)) {
            after = now;
            break;
        }
    }

    const std::string served = std::to_string(binary_server.downloads_served());
    const std::string port = std::to_string(binary_server.port());
    if (!after.ok) {
        return {false, "redeploy_agent: timed out (45 s) waiting for the new agent. Served " +
                           served + " download(s) on port " + port + ", prev_version='" +
                           before.version +
                           "'. Check C:\\ProgramData\\LSW\\lsw-swap.log on the guest for details."};
    }

    nlohmann::json paths = nlohmann::json::array({GuestSwapScript::kGuestAgent});
    if (!mirror_hash.empty()) {
        paths.push_back(GuestSwapScript::kGuestMirror);
    }
    const auto hashes = agent_.request(linkproto::cmd::kFileHashes, {{"paths", paths}});
    if (!hashes.ok) {
        return {false, "redeploy_agent: the new agent answers but not file_hashes (" +
                           hashes.error + "): it is not the one served"};
    }
    const auto in_guest = [&hashes](const char* path) {
        const auto& found = hashes.body.value("hashes", nlohmann::json::object());
        return found.contains(path) && found[path].is_string() ? found[path].get<std::string>()
                                                               : std::string{};
    };
    if (in_guest(GuestSwapScript::kGuestAgent) != agent_hash) {
        return {false, "redeploy_agent: an agent restarted, but C:\\ProgramData\\LSW\\lsw-agent.exe "
                       "is not the one served. Check lsw-swap.log there"};
    }
    std::string mirror_note = binaries.note;
    if (!mirror_hash.empty()) {
        if (in_guest(GuestSwapScript::kGuestMirror) != mirror_hash) {
            return {false, "redeploy_agent: the agent was swapped, but remotewin-guest.exe in the "
                           "guest is not the one served. Check lsw-swap.log there"};
        }
        mirror_note = "remotewin-guest.exe swapped; mirrors started from now on run it, running "
                      "ones keep the old one";
    }
    return {true, "redeploy_agent: success, agent_version '" + before.version + "' -> '" +
                      after.version + "' (" + served + " download(s) served on port " + port +
                      "). " + mirror_note};
}

}  // namespace lsw::host::services
