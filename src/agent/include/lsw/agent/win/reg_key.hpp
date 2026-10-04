#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lsw::agent::win {

// An open registry key, closed when it goes.
//
// The agent reads the Uninstall hive, registers ICDs and writes display
// settings, and each of those carried its own RegOpenKeyExW /
// RegQueryValueExW / RegCloseKey sequence — with a RegCloseKey to
// remember on every ``continue`` of an enumeration loop, and its own idea
// of whether REG_EXPAND_SZ gets expanded.
//
// ``access`` always includes the caller's KEY_WOW64_* view, and a subkey
// is opened with the same flags the caller passes for it: the 32-bit and
// 64-bit views are different keys, and a subkey opened without the view
// silently reads the other one.
class RegKey {
public:
    // ``root\subkey``, or nullopt. ``status`` (optional) receives the
    // Win32 status either way, for a caller that reports it.
    [[nodiscard]] static std::optional<RegKey> open(HKEY root, const wchar_t* subkey, REGSAM access,
                                                    long* status = nullptr);

    // Like ``open``, creating the key (and any missing parents) first.
    [[nodiscard]] static std::optional<RegKey> create(HKEY root, const wchar_t* subkey,
                                                      REGSAM access, long* status = nullptr);

    [[nodiscard]] std::optional<RegKey> open_subkey(const wchar_t* name, REGSAM access) const;

    // A REG_SZ or REG_EXPAND_SZ value; REG_EXPAND_SZ comes back with its
    // %VARIABLES% expanded, so callers see the resolved path. Nullopt
    // when absent, of another type, or zero-length.
    [[nodiscard]] std::optional<std::wstring> read_string(const wchar_t* name) const;

    // A REG_DWORD value, or nullopt when absent or of another type.
    [[nodiscard]] std::optional<DWORD> read_dword(const wchar_t* name) const;

    // Win32 status of the write; ERROR_SUCCESS (0) when it took.
    [[nodiscard]] long write_dword(const wchar_t* name, DWORD value) const;
    [[nodiscard]] long write_qword(const wchar_t* name, std::uint64_t value) const;

    // Names of the immediate subkeys. One that cannot be read is
    // skipped rather than ending the enumeration.
    [[nodiscard]] std::vector<std::wstring> subkey_names() const;

    [[nodiscard]] HKEY get() const noexcept { return key_.get(); }

private:
    explicit RegKey(HKEY key) noexcept : key_(key) {}

    UniqueRegKey key_;
};

}  // namespace lsw::agent::win
