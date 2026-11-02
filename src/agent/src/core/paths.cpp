#include "lsw/agent/core/paths.hpp"

#include <cctype>

namespace lsw::agent::core {

namespace {

// A mirror's log is named after its application; a name longer than this
// is not one anybody reads in a file listing.
constexpr std::size_t kMostLogNameChars = 64;

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
    std::wstring leaf = L"remotewin-guest";
    if (!name.empty()) leaf += L'-';
    for (const char c : name.substr(0, kMostLogNameChars)) {
        const bool kept = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.';
        leaf += kept ? static_cast<wchar_t>(c) : L'_';
    }
    return under_root((leaf + L".log").c_str());
}

std::wstring Paths::remotewin_desktop_log() {
    return under_root(L"remotewin-desktop.log");
}

}  // namespace lsw::agent::core
