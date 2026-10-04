#pragma once

#include <string>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {

// Hot-swaps the guest agent for the locally-built binary, without
// rebooting the VM.
//
// The sequence, in one place because every step depends on the last:
//   1. locate the .exe and read it
//   2. sample the running agent's ``agent_version`` as a baseline
//   3. stand up an AgentBinaryServer the guest can download from
//   4. fire a ``run_script`` that curls it, stops the ``lsw-agent``
//      service, moves the file into place and starts the service again
//   5. poll ``system_info`` until ``agent_version`` changes
//
// Step 5 is the whole reason this is a class and not a fire-and-forget
// RPC: the swap is only real once a *different* binary answers.
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
    link::AgentRpcClient& agent_;
};

}  // namespace lsw::host::mcp
