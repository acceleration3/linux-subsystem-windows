#pragma once

#include <QtGlobal>

#include <string>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {

// Hot-swaps the guest agent for the locally-built binary, and the window
// mirror's guest half beside it, without rebooting the VM.
//
// The sequence, in one place because every step depends on the last:
//   1. locate the agent .exe (required) and remotewin-guest.exe (when
//      there is one) and read them
//   2. sample the running agent's ``agent_version`` as a baseline
//   3. stand up a GuestFileServer the guest can download both from
//   4. fire a ``run_script`` (GuestSwapScript) that curls them, renames
//      the mirror's guest half aside and moves the new one in, stops the
//      ``lsw-agent`` service, moves its file into place and starts the
//      service again
//   5. poll ``system_info`` until ``agent_version`` changes
//   6. ask the new agent for C:\ProgramData\LSW and check that the
//      mirror's guest half there is the size of the one served
//
// Step 5 is the whole reason this is a class and not a fire-and-forget
// RPC: the swap is only real once a *different* binary answers. Step 6
// is the same for the mirror, which has no version to ask: its guest half
// and the host's remotewin check each other's wire version when they
// connect, and refuse a mismatch.
class AgentRedeployer {
public:
    struct Outcome {
        bool ok = false;
        std::string message;  // human-readable, shown to the model either way
    };

    explicit AgentRedeployer(link::AgentRpcClient& agent) noexcept : agent_(agent) {}

    // Blocking; up to ~45 s. Called on an HTTP worker thread.
    [[nodiscard]] Outcome run();

private:
    // The mirror's guest half in the guest, against what was served: a
    // sentence for the outcome either way.
    [[nodiscard]] std::string check_mirror(qsizetype served_size);

    link::AgentRpcClient& agent_;
};

}  // namespace lsw::host::mcp
