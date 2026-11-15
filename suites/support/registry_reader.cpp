#include "support/registry_reader.hpp"

namespace lsw::suites {

namespace {

class Key {
public:
    Key(const std::wstring& path, REGSAM view) {
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ | view, &key_) !=
            ERROR_SUCCESS) {
            key_ = nullptr;
        }
    }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() {
        if (key_ != nullptr) {
            ::RegCloseKey(key_);
        }
    }

    [[nodiscard]] HKEY get() const { return key_; }

private:
    HKEY key_ = nullptr;
};

bool read_raw(HKEY key, const wchar_t* name, DWORD& type, std::vector<wchar_t>& data) {
    DWORD size = 0;
    if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        return false;
    }
    data.assign(size / sizeof(wchar_t) + 2, L'\0');
    return ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(data.data()),
                              &size) == ERROR_SUCCESS;
}

}  // namespace

std::optional<std::wstring> RegistryReader::string(const std::wstring& key,
                                                   const std::wstring& name, REGSAM view) {
    Key handle(key, view);
    DWORD type = 0;
    std::vector<wchar_t> data;
    if (handle.get() == nullptr || !read_raw(handle.get(), name.c_str(), type, data) ||
        (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)) {
        return std::nullopt;
    }
    return std::wstring(data.data());
}

std::optional<DWORD> RegistryReader::dword(const std::wstring& key, const std::wstring& name,
                                           REGSAM view) {
    Key handle(key, view);
    if (handle.get() == nullptr) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (::RegQueryValueExW(handle.get(), name.c_str(), nullptr, &type,
                           reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::wstring> RegistryReader::list(const std::wstring& key, const std::wstring& name,
                                               REGSAM view) {
    std::vector<std::wstring> out;
    Key handle(key, view);
    DWORD type = 0;
    std::vector<wchar_t> data;
    if (handle.get() == nullptr || !read_raw(handle.get(), name.c_str(), type, data)) {
        return out;
    }
    for (const wchar_t* at = data.data(); *at != L'\0'; at += std::wcslen(at) + 1) {
        std::wstring item;
        for (const wchar_t* c = at;; ++c) {
            if (*c == L',' || *c == L'\0') {
                if (!item.empty()) {
                    out.push_back(item);
                }
                item.clear();
                if (*c == L'\0') {
                    break;
                }
            } else {
                item.push_back(*c);
            }
        }
        if (type != REG_MULTI_SZ) {
            break;
        }
    }
    return out;
}

std::vector<RegistryValue> RegistryReader::values(const std::wstring& key, REGSAM view) {
    std::vector<RegistryValue> out;
    Key handle(key, view);
    if (handle.get() == nullptr) {
        return out;
    }
    for (DWORD index = 0;; ++index) {
        wchar_t name[16384];
        DWORD name_size = 16384;
        DWORD type = 0;
        BYTE data[4096];
        DWORD data_size = sizeof(data);
        const LONG result = ::RegEnumValueW(handle.get(), index, name, &name_size, nullptr, &type,
                                            data, &data_size);
        if (result == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA) {
            continue;
        }
        RegistryValue value;
        value.name.assign(name, name_size);
        value.type = type;
        if (result == ERROR_SUCCESS && type == REG_DWORD && data_size >= sizeof(DWORD)) {
            value.number = *reinterpret_cast<DWORD*>(data);
        } else if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            value.text.assign(reinterpret_cast<wchar_t*>(data), data_size / sizeof(wchar_t));
            while (!value.text.empty() && value.text.back() == L'\0') {
                value.text.pop_back();
            }
        }
        out.push_back(std::move(value));
    }
    return out;
}

REGSAM RegistryReader::own_view() {
    return sizeof(void*) == 8 ? kView64 : kView32;
}

}  // namespace lsw::suites
