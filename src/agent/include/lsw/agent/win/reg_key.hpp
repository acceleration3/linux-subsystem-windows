#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <optional>
#include <string>
#include <vector>

namespace lsw::agent::win {
class RegKey {
public:
    [[nodiscard]] static std::optional<RegKey> open(HKEY root, const wchar_t* subkey, REGSAM access,
                                                    long* status = nullptr);

    [[nodiscard]] static std::optional<RegKey> create(HKEY root, const wchar_t* subkey,
                                                      REGSAM access, long* status = nullptr);

    [[nodiscard]] std::optional<RegKey> open_subkey(const wchar_t* name, REGSAM access) const;

    [[nodiscard]] std::optional<std::wstring> read_string(const wchar_t* name) const;

    [[nodiscard]] std::vector<std::wstring> read_strings(const wchar_t* name) const;

    [[nodiscard]] std::optional<DWORD> read_dword(const wchar_t* name) const;

    [[nodiscard]] long write_dword(const wchar_t* name, DWORD value) const;

    [[nodiscard]] long delete_value(const wchar_t* name) const;

    [[nodiscard]] std::vector<std::wstring> subkey_names() const;

    [[nodiscard]] std::vector<std::wstring> value_names() const;

    [[nodiscard]] HKEY get() const noexcept { return key_.get(); }

private:
    explicit RegKey(HKEY key) noexcept : key_(key) {}

    UniqueRegKey key_;
};

}
