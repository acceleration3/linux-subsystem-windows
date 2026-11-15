#pragma once

#include <windows.h>

#include <cwchar>
#include <optional>
#include <string>
#include <vector>

namespace lsw::suites {
struct RegistryValue {
    std::wstring name;
    DWORD type = 0;
    std::wstring text;
    DWORD number = 0;
};

class RegistryReader {
public:
    static constexpr REGSAM kNative = 0;
    static constexpr REGSAM kView64 = KEY_WOW64_64KEY;
    static constexpr REGSAM kView32 = KEY_WOW64_32KEY;

    [[nodiscard]] static std::optional<std::wstring> string(const std::wstring& key,
                                                            const std::wstring& name, REGSAM view);

    [[nodiscard]] static std::optional<DWORD> dword(const std::wstring& key,
                                                    const std::wstring& name, REGSAM view);

    [[nodiscard]] static std::vector<std::wstring> list(const std::wstring& key,
                                                        const std::wstring& name, REGSAM view);

    [[nodiscard]] static std::vector<RegistryValue> values(const std::wstring& key, REGSAM view);

    [[nodiscard]] static REGSAM own_view();
};
}  // namespace lsw::suites
