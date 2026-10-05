#pragma once

#include <string>

namespace lsw::host::mcp {

// The batch file a redeploy runs in the guest: it downloads the new
// binaries from a GuestFileServer at ``10.0.2.2:<port>`` and puts them in
// place under C:\ProgramData\LSW, logging each step to lsw-swap.log there.
//
// Two binaries, swapped two ways. The agent is a service: it is stopped,
// whatever still runs its image is ended, the file is replaced and the
// service started again - started even when the move failed, so a failed
// swap still leaves the old agent running. The window mirror's guest half
// (remotewin-guest.exe) is one process per mirrored application, run from
// the user's session, and nothing is stopped for it: Windows will not
// replace or delete an image a process is running, but it will rename it,
// so the one in place is renamed aside and the new one moved in. Running
// mirrors go on with the old image; the next mirror started runs the new
// one. An aside copy is deleted by the next redeploy that finds it no
// longer running.
class GuestSwapScript {
public:
    // The names the files are served under (GuestFileServer).
    static constexpr const char* kAgentFile = "lsw-guest-agent.exe";
    static constexpr const char* kMirrorFile = "remotewin-guest.exe";

    // ``port`` is the GuestFileServer's; it serves the agent always and
    // the mirror's guest half when ``with_mirror``.
    GuestSwapScript(unsigned short port, bool with_mirror) noexcept
        : port_(port), with_mirror_(with_mirror) {}

    [[nodiscard]] std::string text() const;

private:
    unsigned short port_;
    bool with_mirror_;
};

}  // namespace lsw::host::mcp
