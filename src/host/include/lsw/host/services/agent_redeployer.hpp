#pragma once

#include <QByteArray>

#include <string>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

// Hot-swaps the guest agent for the one LSW ships, and the window
// mirror's guest half beside it, without rebooting the VM. The MCP
// redeploy_agent tool runs it on demand; GuestUpkeep, whenever the guest's
// differ from the ones shipped.
//
// The sequence, in one place because every step depends on the last:
//   1. read the agent .exe (required) and remotewin-guest.exe (when there
//      is one) LSW ships
//   2. sample the running agent's ``agent_version`` and ``agent_started``
//      as a baseline
//   3. stand up a GuestFileServer the guest can download both from
//   4. fire a ``run_script`` (GuestSwapScript) that curls them, renames
//      the mirror's guest half aside and moves the new one in, stops the
//      ``lsw-agent`` service, moves its file into place and starts the
//      service again
//   5. poll ``system_info`` until another agent process answers
//   6. ask it for the SHA-256 of both files in the guest and check they
//      are the ones served
//
// Step 5 is the whole reason this is a class and not a fire-and-forget
// RPC: the swap is only real once another process answers, which is
// what the start time says when the version does not (the same binary
// swapped in, because only the mirror's guest half was new). Step 6 says
// that process runs the agent served, and that the mirror's guest half
// beside it is the one served: it has no version to ask, and its host
// half refuses a guest half of another wire version.
class AgentRedeployer {
public:
    struct Outcome {
        bool ok = false;
        std::string message;  // human-readable, shown to the model either way
    };

    // The binaries LSW ships for the guest, as read from disk: the agent,
    // and the mirror's guest half when there is one. ``error`` says why
    // the agent could not be read; a mirror that could not be is ``note``.
    struct Shipped {
        QByteArray agent;
        QByteArray mirror;
        std::string error;
        std::string note;
    };
    [[nodiscard]] static Shipped shipped();

    // SHA-256 of ``bytes`` in lowercase hex, as the agent's file_hashes
    // answers.
    [[nodiscard]] static std::string sha256(const QByteArray& bytes);

    explicit AgentRedeployer(link::AgentRpcClient& agent) noexcept : agent_(agent) {}

    // Blocking; up to ~45 s. Called on a worker thread.
    [[nodiscard]] Outcome run();

private:
    link::AgentRpcClient& agent_;
};

}  // namespace lsw::host::services
