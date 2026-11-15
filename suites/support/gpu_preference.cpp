#include "support/gpu_preference.hpp"

#include "core/exe_identity.hpp"
#include "core/text.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace lsw::suites {

namespace {

constexpr wchar_t kKey[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
constexpr wchar_t kHighPerformance[] = L"GpuPreference=2;";
constexpr wchar_t kPowerSaving[] = L"GpuPreference=1;";

std::wstring module_path() {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH * 4);
    return std::wstring(buffer, length);
}

std::wstring parent_of(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring rest_of_command_line() {
    const std::wstring line = ::GetCommandLineW();
    std::size_t at = 0;
    if (!line.empty() && line[0] == L'"') {
        at = line.find(L'"', 1);
        at = at == std::wstring::npos ? line.size() : at + 1;
    } else {
        at = line.find(L' ');
        at = at == std::wstring::npos ? line.size() : at;
    }
    return line.substr(at);
}

void set_if_unset(const char* name, const std::string& value) {
    if (std::getenv(name) == nullptr) {
        ::SetEnvironmentVariableA(name, value.c_str());
    }
}

std::string narrow(const std::wstring& text) {
    return Text::utf8(text);
}

void sweep_stale(const std::wstring& root) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    for (DWORD index = 0;;) {
        wchar_t name[MAX_PATH * 2];
        DWORD length = MAX_PATH * 2;
        if (::RegEnumValueW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        const std::wstring value(name, length);
        if (value.compare(0, root.size(), root) == 0 && ::GetFileAttributesW(value.c_str()) == INVALID_FILE_ATTRIBUTES) {
            ::RegDeleteValueW(key, name);
            continue;
        }
        ++index;
    }
    ::RegCloseKey(key);
}

}  // namespace

bool GpuPreferenceScope::needed(const AvAdapter& target) {
    return (target.hybrid_discrete || target.hybrid_integrated) && std::getenv(kMarker) == nullptr;
}

int GpuPreferenceScope::run_again(const AvAdapter& target) {
    const std::wstring original = module_path();
    wchar_t temp[MAX_PATH];
    const DWORD temp_length = ::GetTempPathW(MAX_PATH, temp);
    const std::wstring root = std::wstring(temp, temp_length) + L"lsw-suites";
    ::CreateDirectoryW(root.c_str(), nullptr);
    sweep_stale(root);
    const std::wstring directory = root + L"\\" + std::to_wstring(::GetCurrentProcessId());
    ::CreateDirectoryW(directory.c_str(), nullptr);
    const std::wstring name = original.substr(original.find_last_of(L'\\') + 1);
    const std::wstring copy = directory + L"\\" + name;
    std::vector<std::wstring> copies;
    const auto release = [&] {
        for (const std::wstring& path : copies) {
            ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kKey, path.c_str());
            ::DeleteFileW(path.c_str());
        }
        ::RemoveDirectoryW(directory.c_str());
    };
    const wchar_t* const preference = target.hybrid_discrete ? kHighPerformance : kPowerSaving;
    const DWORD preference_size = static_cast<DWORD>((std::wcslen(preference) + 1) * sizeof(wchar_t));
    std::vector<std::wstring> names{name};
    for (const wchar_t* const sibling : {L"lsw-suites64.exe", L"lsw-suites32.exe"}) {
        if (name != sibling) {
            names.emplace_back(sibling);
        }
    }
    for (const std::wstring& sibling : names) {
        const std::wstring source = sibling == name ? original : parent_of(original) + L"\\" + sibling;
        const std::wstring target_path = directory + L"\\" + sibling;
        if (sibling != name && ::GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        if (!::CopyFileW(source.c_str(), target_path.c_str(), FALSE)) {
            std::fprintf(stderr, "%s: cannot copy the executable for the %s GPU preference: error %lu\n", ExeIdentity::name().c_str(),
                         target.name.c_str(), ::GetLastError());
            release();
            return 2;
        }
        copies.push_back(target_path);
        const LONG set = ::RegSetKeyValueW(HKEY_CURRENT_USER, kKey, target_path.c_str(), REG_SZ, preference, preference_size);
        if (set != ERROR_SUCCESS) {
            std::fprintf(stderr, "%s: cannot set the GPU preference: error %ld\n", ExeIdentity::name().c_str(), set);
            release();
            return 2;
        }
    }
    ::SetEnvironmentVariableA(kMarker, "2");
    set_if_unset("LSW_SUITES_DXC_DIR", narrow(parent_of(original)));
    const std::wstring refs = parent_of(original) + L"\\render-refs";
    if (::GetFileAttributesW(refs.c_str()) != INVALID_FILE_ATTRIBUTES) {
        set_if_unset("LSW_SUITES_REFS", narrow(refs));
    }
    std::wstring command = L"\"" + copy + L"\"" + rest_of_command_line();
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    int code = 2;
    if (::CreateProcessW(copy.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        ::WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 2;
        ::GetExitCodeProcess(process.hProcess, &exit_code);
        code = static_cast<int>(exit_code);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
    } else {
        std::fprintf(stderr, "%s: cannot start the executable under the GPU preference: error %lu\n", ExeIdentity::name().c_str(),
                     ::GetLastError());
    }
    release();
    return code;
}

}  // namespace lsw::suites
