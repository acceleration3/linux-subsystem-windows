#include "support/display_devices.hpp"

#include "core/text.hpp"

// clang-format off
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
// clang-format on

#include <cwchar>

namespace lsw::suites {

namespace {

const GUID kDisplayClass = {0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

std::wstring property_text(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property, bool multi) {
    DWORD type = 0;
    DWORD size = 0;
    ::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, nullptr, 0, &size);
    if (size == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, L'\0');
    if (::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type,
                                            reinterpret_cast<PBYTE>(buffer.data()), size,
                                            nullptr) == FALSE) {
        return {};
    }
    if (!multi) {
        return buffer.data();
    }
    std::wstring joined;
    for (const wchar_t* at = buffer.data(); *at != L'\0'; at += std::wcslen(at) + 1) {
        joined += Text::upper(std::wstring(at)) + L"\n";
    }
    return joined;
}

std::uint32_t hex_after(const std::wstring& id, const wchar_t* tag) {
    const std::size_t at = id.find(tag);
    if (at == std::wstring::npos) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        std::wcstoul(id.substr(at + std::wcslen(tag), 8).c_str(), nullptr, 16));
}

std::uint32_t number_property(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    DWORD value = 0;
    ::SetupDiGetDeviceRegistryPropertyW(set, &device, property, nullptr,
                                        reinterpret_cast<PBYTE>(&value), sizeof(value), nullptr);
    return value;
}

}  // namespace

std::wstring DisplayDevice::class_key() const {
    return L"SYSTEM\\CurrentControlSet\\Control\\Class\\" + driver_key;
}

std::vector<DisplayDevice> DisplayDevices::enumerate(bool present_only) {
    std::vector<DisplayDevice> out;
    HDEVINFO set = ::SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr,
                                          present_only ? DIGCF_PRESENT : 0);
    if (set == INVALID_HANDLE_VALUE) {
        return out;
    }
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; ::SetupDiEnumDeviceInfo(set, index, &device) != FALSE; ++index) {
        DisplayDevice entry;
        std::wstring name = property_text(set, device, SPDRP_FRIENDLYNAME, false);
        if (name.empty()) {
            name = property_text(set, device, SPDRP_DEVICEDESC, false);
        }
        entry.name = Text::utf8(name);
        entry.hardware_ids = property_text(set, device, SPDRP_HARDWAREID, true);
        entry.driver_key = property_text(set, device, SPDRP_DRIVER, false);
        entry.vendor = hex_after(entry.hardware_ids, L"VEN_");
        entry.device = hex_after(entry.hardware_ids, L"DEV_");
        entry.subsystem = hex_after(entry.hardware_ids, L"SUBSYS_");
        entry.accel = entry.hardware_ids.find(L"SUBSYS_52561AF4") != std::wstring::npos;
        entry.bus = number_property(set, device, SPDRP_BUSNUMBER);
        entry.address = number_property(set, device, SPDRP_ADDRESS);
        wchar_t instance[MAX_DEVICE_ID_LEN] = {};
        if (::CM_Get_Device_IDW(device.DevInst, instance, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
            entry.instance_id = instance;
        }
        ULONG status = 0;
        ULONG problem = 0;
        entry.status_known =
            ::CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0) == CR_SUCCESS;
        entry.started = entry.status_known && (status & DN_STARTED) != 0;
        entry.problem_code = entry.status_known && (status & DN_HAS_PROBLEM) != 0
                                 ? static_cast<std::uint32_t>(problem)
                                 : 0;
        wchar_t version[128] = {};
        DEVPROPTYPE type = 0;
        if (::SetupDiGetDevicePropertyW(set, &device, &DEVPKEY_Device_DriverVersion, &type,
                                        reinterpret_cast<PBYTE>(version), sizeof(version), nullptr,
                                        0) != FALSE) {
            entry.driver_version = Text::utf8(version);
        }
        out.push_back(std::move(entry));
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::vector<DisplayInterface> DisplayDevices::interfaces() {
    static const GUID kArrival = {0x1ca05180, 0xa699, 0x450a, {0x9a, 0x0c, 0xde, 0x4f, 0xbe, 0x3d, 0xdd, 0x89}};
    std::vector<DisplayInterface> out;
    HDEVINFO set = ::SetupDiGetClassDevsW(&kArrival, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return out;
    }
    SP_DEVICE_INTERFACE_DATA item{};
    item.cbSize = sizeof(item);
    for (DWORD index = 0; ::SetupDiEnumDeviceInterfaces(set, nullptr, &kArrival, index, &item) != FALSE; ++index) {
        DWORD size = 0;
        ::SetupDiGetDeviceInterfaceDetailW(set, &item, nullptr, 0, &size, nullptr);
        if (size == 0) {
            continue;
        }
        std::vector<char> storage(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (::SetupDiGetDeviceInterfaceDetailW(set, &item, detail, size, nullptr, &device) == FALSE) {
            continue;
        }
        DisplayInterface entry;
        entry.path = detail->DevicePath;
        entry.hardware_ids = property_text(set, device, SPDRP_HARDWAREID, true);
        wchar_t instance[MAX_DEVICE_ID_LEN] = {};
        if (::CM_Get_Device_IDW(device.DevInst, instance, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
            entry.instance_id = instance;
        }
        out.push_back(std::move(entry));
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return out;
}

}  // namespace lsw::suites
