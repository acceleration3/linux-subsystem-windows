#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
// clang-format on

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <cwctype>
#include <optional>
#include <string>
#include <vector>

namespace lsw::agent::rpc::handlers {

namespace {

using util::TextEncoding;
using win::RegKey;

// Every key here is read in the 64-bit view: a 32-bit agent on 64-bit
// Windows would otherwise read SOFTWARE\WOW6432Node and report a policy
// that provisioning set as missing.
constexpr REGSAM kRead = KEY_READ | KEY_WOW64_64KEY;

// A string device property, or empty. REG_MULTI_SZ properties come back
// as their first string, which for hardware IDs is the most specific.
std::wstring device_property(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    DWORD type = 0;
    DWORD needed = 0;
    ::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, nullptr, 0, &needed);
    if (needed == 0 || (type != REG_SZ && type != REG_MULTI_SZ)) {
        return {};
    }
    std::vector<wchar_t> buffer(needed / sizeof(wchar_t) + 2, L'\0');
    if (::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type,
                                            reinterpret_cast<PBYTE>(buffer.data()),
                                            static_cast<DWORD>(buffer.size() * sizeof(wchar_t)),
                                            nullptr)
        == FALSE) {
        return {};
    }
    return std::wstring{buffer.data()};
}

// The installed driver's DriverVersion, from the device's software key
// (SPDRP_DRIVER names it under Control\Class). Empty with no driver.
std::wstring driver_version(HDEVINFO set, SP_DEVINFO_DATA& device) {
    const std::wstring driver_key = device_property(set, device, SPDRP_DRIVER);
    if (driver_key.empty()) {
        return {};
    }
    const std::wstring path = L"SYSTEM\\CurrentControlSet\\Control\\Class\\" + driver_key;
    const auto key = RegKey::open(HKEY_LOCAL_MACHINE, path.c_str(), kRead);
    if (!key) {
        return {};
    }
    return key->read_string(L"DriverVersion").value_or(std::wstring{});
}

// Every string of a REG_MULTI_SZ device property, most specific first.
std::vector<std::wstring> device_strings(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    DWORD type = 0;
    DWORD needed = 0;
    ::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, nullptr, 0, &needed);
    if (needed == 0 || type != REG_MULTI_SZ) {
        return {};
    }
    std::vector<wchar_t> buffer(needed / sizeof(wchar_t) + 2, L'\0');
    if (::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type,
                                            reinterpret_cast<PBYTE>(buffer.data()),
                                            static_cast<DWORD>(buffer.size() * sizeof(wchar_t)),
                                            nullptr)
        == FALSE) {
        return {};
    }
    std::vector<std::wstring> out;
    for (const wchar_t* at = buffer.data(); *at != L'\0'; at += std::wcslen(at) + 1) {
        out.emplace_back(at);
    }
    return out;
}

std::wstring upper(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return text;
}

// The four hex digits after ``tag`` in a PCI hardware ID
// ("PCI\VEN_1002&DEV_7550&SUBSYS_52561AF4&REV_C0"), or 0.
unsigned long hex_after(const std::wstring& id, const wchar_t* tag) {
    const auto at = id.find(tag);
    if (at == std::wstring::npos) {
        return 0;
    }
    return std::wcstoul(id.substr(at + std::wcslen(tag), 4).c_str(), nullptr, 16);
}

// Whether a device is one of accel-virt's adapters. Every virtio-gpu-av-pci
// function keeps the subsystem 1af4:5256 whatever vendor and device it
// presents (the host GPU's, or virtio-gpu's own 1af4:1050 for llvmpipe),
// which is how the driver package recognises it. The service is the
// other half: an adapter bound to avgpu.sys is one whatever its IDs say.
bool is_av_adapter(const std::vector<std::wstring>& hardware_ids, const std::wstring& service) {
    if (upper(service) == L"AVGPU") {
        return true;
    }
    return std::any_of(hardware_ids.begin(), hardware_ids.end(), [](const std::wstring& id) {
        return upper(id).find(L"SUBSYS_52561AF4") != std::wstring::npos;
    });
}

nlohmann::json describe_adapter(HDEVINFO set, SP_DEVINFO_DATA& device,
                                const std::vector<std::wstring>& hardware_ids,
                                const std::wstring& service) {
    // The friendly name is what avgpu.sys publishes from the host GPU
    // ("AMD Radeon RX 9070 XT"); the description is the INF's, which is
    // what an adapter whose driver never started still carries. One with
    // no driver at all has neither, and is named by the host from its IDs.
    std::wstring name = device_property(set, device, SPDRP_FRIENDLYNAME);
    if (name.empty()) {
        name = device_property(set, device, SPDRP_DEVICEDESC);
    }
    const std::wstring id = hardware_ids.empty() ? std::wstring{} : upper(hardware_ids.front());
    ULONG status = 0;
    ULONG problem = 0;
    const bool known =
        ::CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0) == CR_SUCCESS;
    return {
        {"name", TextEncoding::to_utf8(name)},
        {"hardware_id", TextEncoding::to_utf8(id)},
        // What the host matches the adapter to one of its GPUs by.
        {"vendor_id", hex_after(id, L"VEN_")},
        {"device_id", hex_after(id, L"DEV_")},
        // "PCI bus 0, device 5, function 0": tells two identical GPUs apart.
        {"location",
         TextEncoding::to_utf8(device_property(set, device, SPDRP_LOCATION_INFORMATION))},
        {"service", TextEncoding::to_utf8(service)},
        {"driver_version", TextEncoding::to_utf8(driver_version(set, device))},
        // 0 is "no problem"; 28 is "no driver installed", which is what an
        // adapter added after the package was installed reports. A node
        // whose status cannot be read is reported as started=false rather
        // than guessed at.
        {"problem_code", known && (status & DN_HAS_PROBLEM) != 0 ? problem : 0UL},
        {"started", known && (status & DN_STARTED) != 0},
    };
}

// Every accel-virt adapter present, whether or not a driver is bound.
// Enumerated across all classes rather than the display class, because
// an adapter with no driver has no class yet. Empty when nothing can be
// enumerated, which the manager reads as "no adapter" rather than as an
// error, because that is what it is to the user.
nlohmann::json av_adapters() {
    nlohmann::json out = nlohmann::json::array();
    HDEVINFO set =
        ::SetupDiGetClassDevsW(nullptr, L"PCI", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        return out;
    }
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; ::SetupDiEnumDeviceInfo(set, index, &device) != FALSE; ++index) {
        const auto hardware_ids = device_strings(set, device, SPDRP_HARDWAREID);
        const std::wstring service = device_property(set, device, SPDRP_SERVICE);
        if (is_av_adapter(hardware_ids, service)) {
            out.push_back(describe_adapter(set, device, hardware_ids, service));
        }
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::optional<DWORD> read_dword(const wchar_t* subkey, const wchar_t* name) {
    const auto key = RegKey::open(HKEY_LOCAL_MACHINE, subkey, kRead);
    if (!key) {
        return std::nullopt;
    }
    return key->read_dword(name);
}

// TESTSIGNING in the options the kernel booted with. avgpu.sys is
// test-signed, so without it Windows refuses the driver (problem 52).
bool test_signing() {
    const auto key =
        RegKey::open(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", kRead);
    if (!key) {
        return false;
    }
    const std::wstring options =
        upper(key->read_string(L"SystemStartOptions").value_or(std::wstring{}));
    return options.find(L"TESTSIGNING") != std::wstring::npos;
}

// What accel-virt's provision.ps1 NoDriverUpdates writes: Windows Update
// keeps drivers out of quality updates, device installation does not
// search it, and the search order skips it. The device presents the host
// GPU's PCI IDs, so with any of these on Update offers the vendor's real
// driver, which bugchecks on this device.
bool driver_search_off() {
    const auto excluded =
        read_dword(L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate",
                   L"ExcludeWUDriversInQualityUpdate");
    const auto dont_search = read_dword(L"SOFTWARE\\Policies\\Microsoft\\Windows\\DriverSearching",
                                        L"DontSearchWindowsUpdate");
    const auto order = read_dword(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DriverSearching",
                                  L"SearchOrderConfig");
    return excluded.value_or(0) == 1 && dont_search.value_or(0) == 1 && order.value_or(1) == 0;
}

// Whether a WDDM feature is switched on in the global feature database
// (GraphicsDrivers\Features\<id>\Enabled), as provisioning sets it. Null
// when the key is not there at all: that is "never set", not "off".
nlohmann::json feature_enabled(int id) {
    const std::wstring path =
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\Features\\" + std::to_wstring(id);
    const auto enabled = read_dword(path.c_str(), L"Enabled");
    if (!enabled) {
        return nullptr;
    }
    return *enabled != 0;
}

// The driver package's manifest as setup or the last update left it: the
// sha256sum of every file in C:\av\pristine\package, which the manager
// compares with the package it ships. Null when there is none, or when it
// is too large to be a manifest.
nlohmann::json pristine_manifest() {
    constexpr std::streamsize kLimit = 64 * 1024;
    std::ifstream file(std::filesystem::path(L"C:\\av\\pristine\\package\\package.sha256"),
                       std::ios::binary);
    if (!file) {
        return nullptr;
    }
    std::string text(static_cast<std::size_t>(kLimit) + 1, '\0');
    file.read(text.data(), kLimit + 1);
    const std::streamsize got = file.gcount();
    if (got > kLimit) {
        return nullptr;
    }
    text.resize(static_cast<std::size_t>(got));
    return text;
}

}  // namespace

// The guest's side of the Graphics page: every accel-virt adapter, the
// display one and each render-only one, whether a driver is bound to it
// and started, and the pieces of machine state avgpu.sys depends on. Read-only, and cheap: no COM, no
// WMI, one SetupAPI enumeration and a handful of registry reads.
//
//   {"adapters": [{"name", "hardware_id", "vendor_id", "device_id",
//                  "location", "service", "driver_version",
//                  "problem_code", "started"}],
//    "test_signing": bool, "driver_search_off": bool,
//    "user_mode_submission": bool|null, "native_fences": bool|null,
//    "driver_manifest": string|null}
//
// The two feature flags are the policy provisioning wrote (features 4
// and 37). Whether the driver actually got them is its own log's to say.
struct GpuStatusHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& /*params*/, RpcContext& /*ctx*/) override {
        return {
            {"adapters", av_adapters()},
            {"test_signing", test_signing()},
            {"driver_search_off", driver_search_off()},
            {"user_mode_submission", feature_enabled(4)},
            {"native_fences", feature_enabled(37)},
            {"driver_manifest", pristine_manifest()},
        };
    }

    [[nodiscard]] std::string summarize(const nlohmann::json& reply) const override {
        std::string out = std::to_string(reply.value("adapters", nlohmann::json::array()).size())
                          + " adapter(s)";
        for (const auto& adapter : reply.value("adapters", nlohmann::json::array())) {
            out += ", " + adapter.value("service", std::string{}) + " problem "
                   + std::to_string(adapter.value("problem_code", 0UL));
        }
        return out;
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::GpuStatusHandler, ::lsw::linkproto::cmd::kGpuStatus)
