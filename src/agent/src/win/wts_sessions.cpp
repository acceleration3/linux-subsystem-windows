#include "lsw/agent/win/wts_sessions.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"

// clang-format off
#include <windows.h>
#include <wtsapi32.h>
// clang-format on

#include <algorithm>
#include <string>

namespace lsw::agent::win {

namespace {

// One string property of a session, empty when it cannot be read.
std::string query_string(DWORD session, WTS_INFO_CLASS what) {
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;
    if (::WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, what, &buffer, &bytes) ==
            FALSE ||
        buffer == nullptr) {
        return {};
    }
    std::string out = util::TextEncoding::to_utf8(buffer);
    ::WTSFreeMemory(buffer);
    return out;
}

// The state a session is reported in, or nullopt for one that is not a
// user's session to report at all.
std::optional<session::SessionState> state_of(WTS_CONNECTSTATE_CLASS state) {
    switch (state) {
    case WTSActive:
    case WTSShadow:
        return session::SessionState::Active;
    case WTSConnected:
    case WTSConnectQuery:
        return session::SessionState::Connected;
    case WTSDisconnected:
        return session::SessionState::Disconnected;
    default:  // WTSIdle, WTSListen, WTSReset, WTSDown, WTSInit
        return std::nullopt;
    }
}

}  // namespace

std::vector<session::SessionInfo> WtsSessions::list() {
    std::vector<session::SessionInfo> out;
    PWTS_SESSION_INFOW sessions = nullptr;
    DWORD count = 0;
    if (::WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count) == FALSE) {
        LSW_AGENT_WARN("sessions: WTSEnumerateSessions failed err={}", ErrorText::last_error());
        return out;
    }
    const auto console = active_console();
    for (DWORD i = 0; i < count; ++i) {
        const WTS_SESSION_INFOW& s = sessions[i];
        if (s.SessionId == 0) {
            continue;
        }
        const auto state = state_of(s.State);
        if (!state) {
            continue;
        }
        session::SessionInfo info;
        info.id = s.SessionId;
        info.state = *state;
        info.user = query_string(s.SessionId, WTSUserName);
        info.domain = query_string(s.SessionId, WTSDomainName);
        info.console = console && *console == s.SessionId;
        if (info.user.empty()) {
            continue;  // a console at the logon screen: nobody to act for
        }
        out.push_back(std::move(info));
    }
    ::WTSFreeMemory(sessions);
    std::sort(
        out.begin(), out.end(),
        [](const session::SessionInfo& a, const session::SessionInfo& b) { return a.id < b.id; });
    return out;
}

std::optional<std::uint32_t> WtsSessions::active_console() {
    const DWORD id = ::WTSGetActiveConsoleSessionId();
    if (id == 0xFFFFFFFFUL) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(id);
}

std::optional<std::uint32_t> WtsSessions::of_process(std::uint32_t pid) {
    DWORD session = 0;
    if (::ProcessIdToSessionId(pid, &session) == FALSE) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(session);
}

std::uint32_t WtsSessions::current() {
    return of_process(::GetCurrentProcessId()).value_or(0);
}

}  // namespace lsw::agent::win
