#include "support/driver_registry.hpp"

#include "support/registry_reader.hpp"

#include <windows.h>

#include <algorithm>

namespace lsw::suites {

namespace {

std::vector<std::wstring> resolved(const DisplayDevice& device, const wchar_t* name, bool own) {
    std::vector<std::wstring> out;
    for (const std::wstring& item :
         RegistryReader::list(device.class_key(), name, RegistryReader::kView64)) {
        const std::wstring path = DriverRegistry::resolve(item, own);
        if (std::find(out.begin(), out.end(), path) == out.end()) {
            out.push_back(path);
        }
    }
    return out;
}

}  // namespace

std::wstring DriverRegistry::resolve(const std::wstring& name, bool own_bitness) {
    if (name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos) {
        return name;
    }
    wchar_t windows[MAX_PATH];
    const UINT length = ::GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring base(windows, length);
    if (own_bitness) {
        wchar_t found[MAX_PATH];
        if (::SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, found, nullptr) != 0) {
            return found;
        }
        return name;
    }
    return base + (sizeof(void*) == 8 ? L"\\SysWOW64\\" : L"\\Sysnative\\") + name;
}

bool DriverRegistry::exists(const std::wstring& path) {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

DriverFiles DriverRegistry::read(const DisplayDevice& device) {
    DriverFiles files;
    const bool is64 = sizeof(void*) == 8;
    const wchar_t* umd_own = is64 ? L"UserModeDriverName" : L"UserModeDriverNameWow";
    const wchar_t* umd_other = is64 ? L"UserModeDriverNameWow" : L"UserModeDriverName";
    const wchar_t* vk_own = is64 ? L"VulkanDriverName" : L"VulkanDriverNameWow";
    const wchar_t* vk_other = is64 ? L"VulkanDriverNameWow" : L"VulkanDriverName";
    const wchar_t* cl_own = is64 ? L"OpenCLDriverName" : L"OpenCLDriverNameWow";
    const wchar_t* cl_other = is64 ? L"OpenCLDriverNameWow" : L"OpenCLDriverName";
    files.umd_own = resolved(device, umd_own, true);
    files.umd_other = resolved(device, umd_other, false);
    files.vulkan_own = resolved(device, vk_own, true);
    files.vulkan_other = resolved(device, vk_other, false);
    files.opencl_own = resolved(device, cl_own, true);
    files.opencl_other = resolved(device, cl_other, false);
    files.driver_version =
        RegistryReader::string(device.class_key(), L"DriverVersion", RegistryReader::kView64)
            .value_or(L"");
    return files;
}

}  // namespace lsw::suites
