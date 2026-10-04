#pragma once

#include <QString>

namespace lsw::host::platform {

// Finds the cross-built ``lsw-guest-agent.exe`` on the host.
//
// Two consumers with the same question: the wizard bundles it onto the
// payload ISO, and the MCP redeploy tool serves it to a running guest.
// One lookup rule, so a developer who sets ``$LSW_AGENT_EXE`` gets it
// honoured in both.
class AgentExePath {
public:
    AgentExePath() = delete;

    // ``$LSW_AGENT_EXE`` wins if it points at an existing file;
    // otherwise the build tree's, then the installed one under
    // share/lsw/guest, both relative to the manager binary. Empty when
    // none exists.
    [[nodiscard]] static QString locate();
};

}  // namespace lsw::host::platform
