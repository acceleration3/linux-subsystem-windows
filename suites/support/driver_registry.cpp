#include "support/driver_registry.hpp"

#include "support/registry_reader.hpp"

#include <windows.h>

#include "core/text.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

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

std::wstring DriverRegistry::vulkan_library(const std::wstring& manifest) {
    std::ifstream file(std::filesystem::path(manifest), std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const std::string body = text.str();
    const std::size_t key = body.find("\"library_path\"");
    if (key == std::string::npos) {
        return {};
    }
    const std::size_t open = body.find('"', body.find(':', key) + 1);
    if (open == std::string::npos) {
        return {};
    }
    std::string value;
    for (std::size_t i = open + 1; i < body.size() && body[i] != '"'; ++i) {
        if (body[i] == '\\' && i + 1 < body.size()) {
            ++i;
        }
        value += body[i];
    }
    const std::wstring name = Text::wide(value);
    if (name.find(L':') != std::wstring::npos) {
        return name;
    }
    const std::size_t slash = manifest.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? L"" : manifest.substr(0, slash + 1);
    return exists(dir + name) ? dir + name : name;
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
