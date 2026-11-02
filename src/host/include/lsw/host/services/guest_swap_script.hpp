#pragma once

#include <string>

namespace lsw::host::services {
class GuestSwapScript {
public:
    static constexpr const char* kAgentFile = "lsw-guest-agent.exe";
    static constexpr const char* kMirrorFile = "remotewin-guest.exe";
    static constexpr const char* kGuestAgent = "C:\\ProgramData\\LSW\\lsw-agent.exe";
    static constexpr const char* kGuestMirror = "C:\\ProgramData\\LSW\\remotewin-guest.exe";

    GuestSwapScript(unsigned short port, bool with_mirror) noexcept
        : port_(port), with_mirror_(with_mirror) {}

    [[nodiscard]] std::string text() const;

private:
    unsigned short port_;
    bool with_mirror_;
};

}
