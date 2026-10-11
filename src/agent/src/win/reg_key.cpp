#include "lsw/agent/win/reg_key.hpp"

#include "lsw/agent/win/environment.hpp"

#include <iterator>
#include <utility>

namespace lsw::agent::win {

std::optional<RegKey> RegKey::open(HKEY root, const wchar_t* subkey, REGSAM access, long* status) {
    HKEY key = nullptr;
    const LSTATUS rc = ::RegOpenKeyExW(root, subkey, 0, access, &key);
    if (status != nullptr) {
        *status = rc;
    }
    if (rc != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return RegKey{key};
}

std::optional<RegKey> RegKey::create(HKEY root, const wchar_t* subkey, REGSAM access,
                                     long* status) {
    HKEY key = nullptr;
    const LSTATUS rc = ::RegCreateKeyExW(root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, access,
                                         nullptr, &key, nullptr);
    if (status != nullptr) {
        *status = rc;
    }
    if (rc != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return RegKey{key};
}

std::optional<RegKey> RegKey::open_subkey(const wchar_t* name, REGSAM access) const {
    return open(key_.get(), name, access);
}

std::optional<std::wstring> RegKey::read_string(const wchar_t* name) const {
    DWORD type = 0;
    DWORD size = 0;
    LSTATUS rc = ::RegQueryValueExW(key_.get(), name, nullptr, &type, nullptr, &size);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        return std::nullopt;
    }
    std::wstring buf(size / sizeof(wchar_t), L'\0');
    rc = ::RegQueryValueExW(key_.get(), name, nullptr, nullptr, reinterpret_cast<BYTE*>(buf.data()),
                            &size);
    if (rc != ERROR_SUCCESS) {
        return std::nullopt;
    }
    // Registry strings may or may not carry their terminator, and may
    // carry several.
    while (!buf.empty() && buf.back() == L'\0') {
        buf.pop_back();
    }
    if (type == REG_EXPAND_SZ && !buf.empty()) {
        return Environment::expand(buf);
    }
    return buf;
}

std::optional<DWORD> RegKey::read_dword(const wchar_t* name) const {
    DWORD data = 0;
    DWORD size = sizeof(data);
    DWORD type = 0;
    if (::RegQueryValueExW(key_.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(&data),
                           &size) != ERROR_SUCCESS ||
        type != REG_DWORD) {
        return std::nullopt;
    }
    return data;
}

long RegKey::write_dword(const wchar_t* name, DWORD value) const {
    return ::RegSetValueExW(key_.get(), name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                            sizeof(value));
}

long RegKey::delete_value(const wchar_t* name) const {
    return ::RegDeleteValueW(key_.get(), name);
}

std::vector<std::wstring> RegKey::subkey_names() const {
    std::vector<std::wstring> names;
    // 255 characters is the registry's own limit on a key name.
    wchar_t name[256];
    for (DWORD index = 0;; ++index) {
        DWORD length = static_cast<DWORD>(std::size(name));
        const LSTATUS rc =
            ::RegEnumKeyExW(key_.get(), index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc == ERROR_SUCCESS) {
            names.emplace_back(name, length);
        }
    }
    return names;
}

}  // namespace lsw::agent::win
