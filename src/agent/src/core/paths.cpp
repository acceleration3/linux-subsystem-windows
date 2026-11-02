#include "lsw/agent/core/paths.hpp"

#include <cctype>

namespace lsw::agent::core {

namespace {
constexpr std::size_t kMostLogNameChars = 64;

[[nodiscard]] std::wstring under_root(const wchar_t* leaf) {
    return Paths::root() + L"\\" + leaf;
}

}

std::wstring Paths::root() {
    return L"C:\\ProgramData\\LSW";
}

std::wstring Paths::logs_dir() {
    return under_root(L"logs");
}

std::wstring Paths::mirror_logs_dir() {
    return logs_dir() + L"\\mirrors";
}

std::wstring Paths::driver_logs_dir() {
    return L"C:\\ProgramData\\av";
}

std::wstring Paths::agent_log() {
    return logs_dir() + L"\\agent.log";
}

std::wstring Paths::session_helper_log(std::uint32_t session) {
    return logs_dir() + L"\\agent-session-" + std::to_wstring(session) + L".log";
}

std::wstring Paths::desktop_ready_flag() {
    return under_root(L"desktop-ready.flag");
}

std::wstring Paths::release_file() {
    return under_root(L"release.json");
}

std::wstring Paths::update_result_file() {
    return under_root(L"update-result.json");
}

std::wstring Paths::work_dir() {
    return under_root(L"work");
}

std::wstring Paths::remotewin_guest_exe() {
    return under_root(L"remotewin-guest.exe");
}

std::wstring Paths::remotewin_guest_log(std::string_view name) {
    std::wstring leaf;
    for (const char c : name.substr(0, kMostLogNameChars)) {
        const bool kept = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.';
        leaf += kept ? static_cast<wchar_t>(c) : L'_';
    }
    if (leaf.empty()) leaf = L"app";
    return mirror_logs_dir() + L"\\" + leaf + L".log";
}

std::wstring Paths::remotewin_desktop_log() {
    return mirror_logs_dir() + L"\\desktop.log";
}

}
