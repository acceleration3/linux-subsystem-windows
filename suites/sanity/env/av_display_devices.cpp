#include "sanity/env/av_display_devices.hpp"

#include "core/text.hpp"

// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
// clang-format on

#include <cwchar>

namespace lsw::suites::sanity::env {

namespace {

std::vector<std::wstring> property_strings(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    std::vector<std::wstring> out;
    DWORD type = 0;
    DWORD size = 0;
    ::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, nullptr, 0, &size);
    if (size == 0) {
        return out;
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, L'\0');
    if (::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type,
                                            reinterpret_cast<PBYTE>(buffer.data()), size,
                                            nullptr) == FALSE) {
        return out;
    }
    for (const wchar_t* at = buffer.data(); *at != L'\0'; at += std::wcslen(at) + 1) {
        out.emplace_back(at);
    }
    return out;
}

std::uint32_t hex_after(const std::wstring& id, const wchar_t* tag) {
    const std::size_t at = id.find(tag);
    if (at == std::wstring::npos) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        std::wcstoul(id.substr(at + std::wcslen(tag), 4).c_str(), nullptr, 16));
}

}  // namespace

std::vector<AvDisplayDevice> AvDisplayDevices::enumerate() {
    std::vector<AvDisplayDevice> out;
    HDEVINFO set =
        ::SetupDiGetClassDevsW(nullptr, L"PCI", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        return out;
    }
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; ::SetupDiEnumDeviceInfo(set, index, &device) != FALSE; ++index) {
        const std::vector<std::wstring> ids = property_strings(set, device, SPDRP_HARDWAREID);
        std::wstring matched;
        for (const std::wstring& id : ids) {
            if (Text::upper(id).find(L"SUBSYS_52561AF4") != std::wstring::npos) {
                matched = Text::upper(id);
                break;
            }
        }
        if (matched.empty()) {
            continue;
        }
        std::vector<std::wstring> names = property_strings(set, device, SPDRP_FRIENDLYNAME);
        if (names.empty()) {
            names = property_strings(set, device, SPDRP_DEVICEDESC);
        }
        AvDisplayDevice entry;
        entry.name = names.empty() ? std::string() : Text::utf8(names.front());
        entry.hardware_id = Text::utf8(matched);
        entry.vendor = hex_after(matched, L"VEN_");
        entry.device = hex_after(matched, L"DEV_");
        ULONG status = 0;
        ULONG problem = 0;
        entry.status_known =
            ::CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0) == CR_SUCCESS;
        entry.started = entry.status_known && (status & DN_STARTED) != 0;
        entry.problem_code =
            entry.status_known && (status & DN_HAS_PROBLEM) != 0 ? static_cast<std::uint32_t>(problem) : 0;
        out.push_back(std::move(entry));
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return out;
}

}  // namespace lsw::suites::sanity::env
