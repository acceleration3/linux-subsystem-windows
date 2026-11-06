#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/process_info.hpp"
#include "lsw/agent/win/reg_key.hpp"
#include "lsw/agent/win/wts_sessions.hpp"
#include "lsw/linkproto/commands.hpp"

// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <tlhelp32.h>
// clang-format on

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lsw::agent::rpc::handlers {

namespace {

using util::TextEncoding;
using win::ProcessInfo;
using win::RegKey;
using win::WtsSessions;

constexpr REGSAM kRead = KEY_READ | KEY_WOW64_64KEY;

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

unsigned long hex_after(const std::wstring& id, const wchar_t* tag) {
    const auto at = id.find(tag);
    if (at == std::wstring::npos) {
        return 0;
    }
    return std::wcstoul(id.substr(at + std::wcslen(tag), 4).c_str(), nullptr, 16);
}

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
        {"vendor_id", hex_after(id, L"VEN_")},
        {"device_id", hex_after(id, L"DEV_")},
        {"location",
         TextEncoding::to_utf8(device_property(set, device, SPDRP_LOCATION_INFORMATION))},
        {"service", TextEncoding::to_utf8(service)},
        {"driver_version", TextEncoding::to_utf8(driver_version(set, device))},
        {"problem_code", known && (status & DN_HAS_PROBLEM) != 0 ? problem : 0UL},
        {"started", known && (status & DN_STARTED) != 0},
    };
}

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

bool display_core_disabled() {
    return read_dword(L"SOFTWARE\\Microsoft\\Windows\\Dwm", L"DisableDDisplay").value_or(0) == 1;
}

std::optional<bool> has_module(DWORD pid, const wchar_t* module) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        const HANDLE snap =
            ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) {
            if (::GetLastError() == ERROR_BAD_LENGTH) {
                continue;
            }
            return std::nullopt;
        }
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (::Module32FirstW(snap, &entry) != FALSE) {
            do {
                if (::_wcsicmp(entry.szModule, module) == 0) {
                    found = true;
                    break;
                }
            } while (::Module32NextW(snap, &entry) != FALSE);
        }
        ::CloseHandle(snap);
        return found;
    }
    return std::nullopt;
}

nlohmann::json display_core_loaded() {
    const auto console = WtsSessions::active_console();
    if (!console) {
        return nullptr;
    }
    for (const auto& process : ProcessInfo::snapshot()) {
        if (::_wcsicmp(process.exe_name.c_str(), L"dwm.exe") != 0
            || WtsSessions::of_process(process.pid) != console) {
            continue;
        }
        const auto loaded = has_module(process.pid, L"ddisplay.dll");
        if (!loaded) {
            return nullptr;
        }
        return *loaded;
    }
    return nullptr;
}

nlohmann::json feature_enabled(int id) {
    const std::wstring path =
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\Features\\" + std::to_wstring(id);
    const auto enabled = read_dword(path.c_str(), L"Enabled");
    if (!enabled) {
        return nullptr;
    }
    return *enabled != 0;
}

}

struct GpuStatusHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& , RpcContext& ) override {
        nlohmann::json adapters = av_adapters();
        return {
            {"adapters", std::move(adapters)},
            {"test_signing", test_signing()},
            {"driver_search_off", driver_search_off()},
            {"display_core_disabled", display_core_disabled()},
            {"display_core_loaded", display_core_loaded()},
            {"user_mode_submission", feature_enabled(4)},
            {"native_fences", feature_enabled(37)},
        };
    }

    [[nodiscard]] log::Level request_level() const override { return log::Level::Trace; }

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

}

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::GpuStatusHandler, ::lsw::linkproto::cmd::kGpuStatus)
