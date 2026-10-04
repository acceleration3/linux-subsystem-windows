#include "lsw/agent/core/paths.hpp"

namespace lsw::agent::core {

namespace {

[[nodiscard]] std::wstring under_root(const wchar_t* leaf) {
    return Paths::root() + L"\\" + leaf;
}

}  // namespace

std::wstring Paths::root() {
    return L"C:\\ProgramData\\LSW";
}

std::wstring Paths::agent_log() {
    return under_root(L"agent.log");
}

std::wstring Paths::session_helper_log(std::uint32_t session) {
    return root() + L"\\agent-session-" + std::to_wstring(session) + L".log";
}

std::wstring Paths::desktop_probe_log() {
    return under_root(L"desktop-probe.log");
}

std::wstring Paths::desktop_ready_flag() {
    return under_root(L"desktop-ready.flag");
}

std::wstring Paths::work_dir() {
    return under_root(L"work");
}

std::wstring Paths::remotewin_guest_exe() {
    return under_root(L"remotewin-guest.exe");
}

std::wstring Paths::remotewin_guest_log() {
    return under_root(L"remotewin-guest.log");
}

}  // namespace lsw::agent::core
