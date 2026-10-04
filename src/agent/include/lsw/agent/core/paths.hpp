#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::core {

// Where the agent keeps its files on the guest's disk.
//
// Everything lives under ``C:\ProgramData\LSW``: the provisioning ISO
// copies the agent there, the autounattend grants Users:Modify on it
// (which is why staged setup files go there rather than to %TEMP% — see
// WorkDir), and every diagnostic a remote session reads is a file in it.
// Spelled out in six places, the path was one rename away from the agent
// writing its log somewhere the tools no longer look.
class Paths {
public:
    Paths() = delete;

    // ``C:\ProgramData\LSW``, no trailing separator.
    [[nodiscard]] static std::wstring root();

    // ``agent.log`` — the file side of the log ring.
    [[nodiscard]] static std::wstring agent_log();

    // ``agent-session-<N>.log`` — the session helper's own file in
    // session N. Its records also reach agent.log through the service,
    // once it is connected; this file has what happened before that.
    [[nodiscard]] static std::wstring session_helper_log(std::uint32_t session);

    // ``desktop-probe.log`` — DesktopProbe's session snapshots.
    [[nodiscard]] static std::wstring desktop_probe_log();

    // ``desktop-ready.flag`` — present once the desktop gate has opened
    // on this install; its absence is what "first boot" means.
    [[nodiscard]] static std::wstring desktop_ready_flag();

    // ``work`` — staged setup files (see WorkDir).
    [[nodiscard]] static std::wstring work_dir();

    // The window mirror's guest half, and where it logs.
    [[nodiscard]] static std::wstring remotewin_guest_exe();
    [[nodiscard]] static std::wstring remotewin_guest_log();
};

}  // namespace lsw::agent::core
