#include "lsw/agent/win/environment.hpp"

#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"

#include <map>
#include <windows.h>

namespace lsw::agent::win {

std::wstring Environment::expand(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const DWORD n = ::ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (n == 0) {
        return s;
    }
    std::wstring out(n, L'\0');
    if (::ExpandEnvironmentStringsW(s.c_str(), out.data(), n) == 0) {
        return s;
    }
    while (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

std::vector<wchar_t> Environment::block_with(const std::vector<Override>& overrides) {
    if (overrides.empty()) {
        return {};
    }
    wchar_t* block = ::GetEnvironmentStringsW();
    std::vector<wchar_t> out = block_from(block, overrides);
    if (block != nullptr) {
        ::FreeEnvironmentStringsW(block);
    }
    return out;
}

std::vector<wchar_t> Environment::block_from(const wchar_t* base,
                                             const std::vector<Override>& overrides) {
    // Existing variables, keyed by upper-cased name so an override
    // replaces a differently-cased one rather than adding a twin.
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> vars;  // KEY -> (name, value)
    if (base != nullptr) {
        for (const wchar_t* p = base; *p != L'\0';) {
            const std::wstring entry(p);
            p += entry.size() + 1;
            // Skip the "=C:=C:\..." drive-current-directory entries:
            // they start with '=' and are the shell's business.
            const auto eq = entry.find(L'=', 1);
            if (entry.empty() || entry[0] == L'=' || eq == std::wstring::npos) {
                continue;
            }
            const std::wstring name = entry.substr(0, eq);
            vars[util::WideString::upper(name)] = {name, entry.substr(eq + 1)};
        }
    }
    for (const auto& [name, value] : overrides) {
        const std::wstring wname = util::TextEncoding::to_wide(name);
        if (wname.empty()) {
            continue;
        }
        vars[util::WideString::upper(wname)] = {wname, util::TextEncoding::to_wide(value)};
    }

    std::vector<wchar_t> out;
    for (const auto& [key, pair] : vars) {  // std::map iterates sorted
        const std::wstring entry = pair.first + L"=" + pair.second;
        out.insert(out.end(), entry.begin(), entry.end());
        out.push_back(L'\0');
    }
    if (out.empty()) {
        out.push_back(L'\0');  // an empty block is still two NULs
    }
    out.push_back(L'\0');  // block terminator
    return out;
}

}  // namespace lsw::agent::win
