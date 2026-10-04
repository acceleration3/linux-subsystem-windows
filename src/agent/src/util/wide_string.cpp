#include "lsw/agent/util/wide_string.hpp"

#include <cwctype>

namespace lsw::agent::util {

namespace {

[[nodiscard]] bool iequal(wchar_t a, wchar_t b) {
    return std::towlower(static_cast<std::wint_t>(a)) == std::towlower(static_cast<std::wint_t>(b));
}

}  // namespace

std::wstring WideString::lower(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
    }
    return s;
}

std::wstring WideString::upper(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(std::towupper(static_cast<std::wint_t>(c)));
    }
    return s;
}

bool WideString::istarts_with(std::wstring_view s, std::wstring_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (!iequal(s[i], prefix[i])) {
            return false;
        }
    }
    return true;
}

bool WideString::iends_with(std::wstring_view s, std::wstring_view suffix) {
    if (s.size() < suffix.size()) {
        return false;
    }
    return istarts_with(s.substr(s.size() - suffix.size()), suffix);
}

std::optional<std::wstring> WideString::parent_dir(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::nullopt;
    }
    return path.substr(0, slash);
}

}  // namespace lsw::agent::util
