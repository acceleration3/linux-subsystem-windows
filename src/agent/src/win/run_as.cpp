#include "lsw/agent/win/run_as.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/token_privileges.hpp"
#include "lsw/agent/win/unique_handle.hpp"
#include "lsw/agent/win/wts_sessions.hpp"

// clang-format off
#include <windows.h>
#include <sddl.h>
#include <userenv.h>
#include <wtsapi32.h>
// clang-format on

#include <stdexcept>
#include <vector>

namespace lsw::agent::win {

namespace {

// The interactive desktop of the session the token belongs to.
constexpr const wchar_t* kInteractiveDesktop = L"winsta0\\default";

// The longest string ExpandEnvironmentStrings* will produce.
constexpr DWORD kMaxExpanded = 32768;

std::vector<unsigned char> token_info(HANDLE token, TOKEN_INFORMATION_CLASS what) {
    DWORD needed = 0;
    ::GetTokenInformation(token, what, nullptr, 0, &needed);
    if (needed == 0) {
        return {};
    }
    std::vector<unsigned char> buffer(needed);
    if (::GetTokenInformation(token, what, buffer.data(), needed, &needed) == FALSE) {
        return {};
    }
    return buffer;
}

std::wstring sid_string(PSID sid) {
    LPWSTR text = nullptr;
    if (::ConvertSidToStringSidW(sid, &text) == FALSE || text == nullptr) {
        return {};
    }
    std::wstring out{text};
    ::LocalFree(text);
    return out;
}

std::string account_of(PSID sid) {
    wchar_t name[256] = {};
    wchar_t domain[256] = {};
    DWORD name_len = 256;
    DWORD domain_len = 256;
    SID_NAME_USE use{};
    if (::LookupAccountSidW(nullptr, sid, name, &name_len, domain, &domain_len, &use) == FALSE) {
        return {};
    }
    return util::TextEncoding::to_utf8(name);
}

bool is_elevated(HANDLE token) {
    TOKEN_ELEVATION elevation{};
    DWORD len = 0;
    return ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &len) !=
               FALSE &&
           elevation.TokenIsElevated != 0;
}

// The user's full token when UAC split their logon into a filtered and a
// linked one; null when there is no split (UAC off, or not an admin).
HANDLE linked_token(HANDLE token) {
    TOKEN_ELEVATION_TYPE type{};
    DWORD len = 0;
    if (::GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &len) == FALSE ||
        type != TokenElevationTypeLimited) {
        return nullptr;
    }
    TOKEN_LINKED_TOKEN linked{};
    if (::GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &len) == FALSE) {
        LSW_AGENT_WARN("run_as: no linked token err={}", ErrorText::last_error());
        return nullptr;
    }
    return linked.LinkedToken;
}

// A primary token of our own, so its privileges and session can be set
// without touching the logon's.
UniqueHandle primary_copy(HANDLE token) {
    UniqueHandle out;
    if (::DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary,
                           out.put()) == FALSE) {
        out.reset();
    }
    return out;
}

void close_token(void* token) {
    if (token != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(token));
    }
}

}  // namespace

RunAs RunAs::self() {
    RunAs out;
    out.session_ = WtsSessions::current();
    UniqueHandle token;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.put()) != FALSE) {
        out.elevated_ = is_elevated(token.get());
        const auto user = token_info(token.get(), TokenUser);
        if (!user.empty()) {
            const auto* tu = reinterpret_cast<const TOKEN_USER*>(user.data());
            out.sid_ = sid_string(tu->User.Sid);
            out.account_ = account_of(tu->User.Sid);
        }
    }
    return out;
}

RunAs RunAs::user(std::uint32_t session, bool elevated) {
    const std::string where = "session " + std::to_string(session);
    UniqueHandle logon;
    if (::WTSQueryUserToken(session, logon.put()) == FALSE) {
        const DWORD err = ::GetLastError();
        logon.reset();
        if (err == ERROR_NO_TOKEN) {
            throw std::runtime_error("no user is logged on to " + where);
        }
        if (err == ERROR_PRIVILEGE_NOT_HELD) {
            throw std::runtime_error("cannot act for " + where +
                                     ": the agent is not running as LocalSystem "
                                     "(WTSQueryUserToken needs SeTcbPrivilege)");
        }
        throw std::runtime_error("WTSQueryUserToken(" + where +
                                 ") failed: " + ErrorText::win32(err));
    }

    UniqueHandle linked{elevated ? linked_token(logon.get()) : nullptr};
    UniqueHandle primary;
    if (linked) {
        primary = primary_copy(linked.get());
        if (!primary) {
            // Only a caller with SeTcbPrivilege gets a usable linked
            // token; without one it is identification-level and cannot
            // become a primary token. Better a filtered launch than none.
            LSW_AGENT_WARN("run_as: the linked token of {} cannot be used (err={}); "
                           "starting the process unelevated",
                           where, ErrorText::last_error());
        }
    }
    if (!primary) {
        primary = primary_copy(logon.get());
    }
    if (!primary) {
        throw std::runtime_error("DuplicateTokenEx(" + where +
                                 ") failed: " + ErrorText::last_error());
    }

    // The linked token is the same logon, but nothing promises its session
    // id says so; a process started with the wrong one lands on another
    // session's desktop, or on none.
    DWORD session_id = session;
    if (::SetTokenInformation(primary.get(), TokenSessionId, &session_id, sizeof(session_id)) ==
        FALSE) {
        LSW_AGENT_WARN("run_as: could not pin the token to {} (err={})", where,
                       ErrorText::last_error());
    }

    RunAs out;
    out.session_ = session;
    out.elevated_ = is_elevated(primary.get());
    if (out.elevated_) {
        (void)TokenPrivileges::enable_install_privileges(primary.get());
    }
    const auto user = token_info(primary.get(), TokenUser);
    if (!user.empty()) {
        const auto* tu = reinterpret_cast<const TOKEN_USER*>(user.data());
        out.sid_ = sid_string(tu->User.Sid);
        out.account_ = account_of(tu->User.Sid);
    }
    out.token_ = std::shared_ptr<void>(primary.release(), &close_token);
    return out;
}

std::wstring RunAs::expand(const std::wstring& s) const {
    if (token_ == nullptr || s.empty()) {
        return Environment::expand(s);
    }
    std::vector<wchar_t> buffer(kMaxExpanded, L'\0');
    if (::ExpandEnvironmentStringsForUserW(static_cast<HANDLE>(token_.get()), s.c_str(),
                                           buffer.data(), kMaxExpanded) == FALSE) {
        return s;  // unchanged, as Environment::expand does
    }
    return std::wstring{buffer.data()};
}

std::vector<wchar_t> RunAs::environment(const std::vector<Environment::Override>& overrides) const {
    if (token_ == nullptr) {
        return Environment::block_with(overrides);
    }
    void* block = nullptr;
    if (::CreateEnvironmentBlock(&block, static_cast<HANDLE>(token_.get()), FALSE) == FALSE ||
        block == nullptr) {
        throw std::runtime_error("CreateEnvironmentBlock(" + describe() +
                                 ") failed: " + ErrorText::last_error());
    }
    std::vector<wchar_t> out =
        Environment::block_from(static_cast<const wchar_t*>(block), overrides);
    ::DestroyEnvironmentBlock(block);
    return out;
}

void RunAs::apply(SpawnOptions& options) const {
    if (token_ == nullptr) {
        return;
    }
    options.token = static_cast<HANDLE>(token_.get());
    options.desktop = kInteractiveDesktop;
    if (options.environment.empty()) {
        options.environment = environment({});
    }
}

std::string RunAs::describe() const {
    std::string out = (account_.empty() ? std::string{"?"} : account_) + " in session " +
                      std::to_string(session_);
    if (token_ != nullptr && elevated_) {
        out += " (elevated)";
    }
    return out;
}

}  // namespace lsw::agent::win
