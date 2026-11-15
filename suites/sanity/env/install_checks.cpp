#include "sanity/env/install_checks.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/display_devices.hpp"
#include "support/driver_registry.hpp"
#include "support/event_log_scan.hpp"
#include "support/file_version.hpp"
#include "support/kmt_api.hpp"
#include "support/problems.hpp"
#include "support/registry_reader.hpp"
#include "support/vulkan_api.hpp"
#include "support/opencl_api.hpp"
#include "support/opengl_context.hpp"
#include "support/test_window.hpp"

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <algorithm>
#include <chrono>
#include <set>

namespace lsw::suites::sanity::env {

namespace {

constexpr std::uint32_t kProblemUnsignedDriver = 52;

std::vector<DisplayDevice> accel_devices() {
    std::vector<DisplayDevice> out;
    for (DisplayDevice& device : DisplayDevices::enumerate(true)) {
        if (device.accel) {
            out.push_back(std::move(device));
        }
    }
    return out;
}

std::string narrow(const std::wstring& text) {
    return Text::utf8(text);
}

bool loads(const std::wstring& path, std::string& error) {
    HMODULE module = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        error = narrow(path) + " does not load (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    ::FreeLibrary(module);
    return true;
}

bool dwm_has_display_core(bool& known) {
    known = false;
    const DWORD console = ::WTSGetActiveConsoleSessionId();
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    DWORD pid = 0;
    if (::Process32FirstW(snapshot, &process) != FALSE) {
        do {
            DWORD session = 0;
            if (::_wcsicmp(process.szExeFile, L"dwm.exe") == 0 &&
                ::ProcessIdToSessionId(process.th32ProcessID, &session) != FALSE && session == console) {
                pid = process.th32ProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &process) != FALSE);
    }
    ::CloseHandle(snapshot);
    if (pid == 0) {
        return false;
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        const HANDLE modules = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (modules == INVALID_HANDLE_VALUE) {
            if (::GetLastError() == ERROR_BAD_LENGTH) {
                continue;
            }
            return false;
        }
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (::Module32FirstW(modules, &entry) != FALSE) {
            do {
                if (::_wcsicmp(entry.szModule, L"ddisplay.dll") == 0) {
                    found = true;
                    break;
                }
            } while (::Module32NextW(modules, &entry) != FALSE);
        }
        ::CloseHandle(modules);
        known = true;
        return found;
    }
    return false;
}

std::optional<DWORD> code_integrity_options() {
    struct CodeIntegrityInformation {
        ULONG length;
        ULONG options;
    };
    using Query = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return std::nullopt;
    }
    void* address = reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtQuerySystemInformation"));
    auto query = reinterpret_cast<Query>(address);
    if (query == nullptr) {
        return std::nullopt;
    }
    CodeIntegrityInformation info{sizeof(info), 0};
    ULONG returned = 0;
    if (query(103, &info, sizeof(info), &returned) < 0) {
        return std::nullopt;
    }
    return info.options;
}

}  // namespace

Outcome InstallChecks::adapter_started(Context& context) {
    const std::vector<DisplayDevice> devices = DisplayDevices::enumerate(true);
    context.measure("display.devices", devices.size());
    if (devices.empty()) {
        return Outcome::fail("SetupAPI lists no display adapter");
    }
    Problems problems;
    for (const DisplayDevice& device : devices) {
        const std::string label = device.name + " (" + narrow(device.instance_id) + ")";
        if (!device.status_known) {
            problems.add(label + ": device status unreadable");
        } else if (device.problem_code != 0) {
            problems.add(label + ": problem code " + std::to_string(device.problem_code));
        } else if (!device.started) {
            problems.add(label + ": not started");
        }
    }
    return problems.outcome();
}

Outcome InstallChecks::driver_version_consistent(Context& context) {
    const std::vector<DisplayDevice> devices = accel_devices();
    if (devices.empty()) {
        return Outcome::fail("no accel-virt display device in SetupAPI");
    }
    std::vector<AvAdapter> adapters;
    std::string error;
    KmtApi kmt;
    if (!AvAdapters::accel(adapters, error) || !kmt.load(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (const DisplayDevice& device : devices) {
        context.measure("setupapi.driver_ver", device.driver_version);
        const DriverFiles files = DriverRegistry::read(device);
        const auto note_files = [&](const std::vector<std::wstring>& paths) {
            for (const std::wstring& path : paths) {
                const std::optional<std::uint64_t> version = FileVersion::read(path);
                context.measure("file." + narrow(path), version ? FileVersion::text(*version) : "none");
            }
        };
        note_files(files.umd_own);
        note_files(files.umd_other);
        note_files(files.opencl_own);
        note_files(files.opencl_other);
        for (const std::wstring& manifest : files.vulkan_own) {
            note_files({DriverRegistry::vulkan_library(manifest)});
        }
    }
    std::set<std::uint64_t> release;
    for (const AvAdapter& adapter : adapters) {
        if (!adapter.umd_version_known) {
            problems.add("DXGI CheckInterfaceSupport fails for " + adapter.name);
            continue;
        }
        context.measure("dxgi.version." + adapter.luid_text(), FileVersion::text(adapter.umd_version));
        std::wstring loaded = kmt.umd_name(adapter.luid, 2);
        if (loaded.empty()) {
            problems.add(adapter.name + ": D3DKMT cannot name the loaded D3D11 user-mode driver");
            continue;
        }
        loaded = DriverRegistry::resolve(loaded, true);
        const std::optional<std::uint64_t> version = FileVersion::read(loaded);
        if (!version) {
            problems.add(narrow(loaded) + " has no version resource");
        } else if (*version != adapter.umd_version) {
            problems.add("DXGI reports " + FileVersion::text(adapter.umd_version) + " for " + adapter.name + " but " +
                         narrow(loaded) + " is " + FileVersion::text(*version));
        } else {
            release.insert(*version);
        }
    }
    std::vector<std::pair<std::string, std::string>> reported;
    OpenClApi opencl;
    std::vector<OpenClPlatform> platforms;
    if (opencl.load(error) && opencl.platforms(platforms, error)) {
        for (const OpenClPlatform& platform : platforms) {
            for (const OpenClDevice& device : platform.devices) {
                reported.emplace_back("CL_DRIVER_VERSION", device.driver_version);
            }
        }
    }
    VulkanApi vulkan;
    std::vector<VulkanDevice> vk_devices;
    if (vulkan.load(error) && vulkan.create_instance(error) && vulkan.devices(vk_devices, error)) {
        for (const VulkanDevice& device : vk_devices) {
            context.measure("vulkan.driverVersion." + device.name, std::to_string(device.driver_version >> 22) + "." +
                                                                       std::to_string((device.driver_version >> 12) & 0x3ff) +
                                                                       "." + std::to_string(device.driver_version & 0xfff));
        }
    }
    {
        TestWindow window;
        OpenGlContext gl;
        if (window.create({}, error) && gl.create(window.dc(), error)) {
            reported.emplace_back("GL_VERSION", gl.version());
        }
    }
    for (const auto& [api, text] : reported) {
        context.measure("reported." + api, text);
        for (std::size_t at = 0; at < text.size(); ++at) {
            if (text[at] < '0' || text[at] > '9' || (at > 0 && (text[at - 1] == '.' || (text[at - 1] >= '0' && text[at - 1] <= '9')))) {
                continue;
            }
            std::size_t end = at;
            int dots = 0;
            while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || (text[end] == '.' && dots < 3))) {
                dots += text[end] == '.' ? 1 : 0;
                ++end;
            }
            const std::string token = text.substr(at, end - at);
            const std::optional<std::uint64_t> version = dots == 3 ? FileVersion::parse(token) : std::nullopt;
            if (version && !release.empty() && release.count(*version) == 0) {
                problems.add(api + " '" + text + "' carries driver version " + token + " which is not the release " +
                             FileVersion::text(*release.begin()));
            }
            at = end;
        }
    }
    return problems.outcome();
}

Outcome InstallChecks::umd_files_present_both_bitnesses(Context& context) {
    const std::vector<DisplayDevice> devices = accel_devices();
    if (devices.empty()) {
        return Outcome::fail("no accel-virt display device in SetupAPI");
    }
    Problems problems;
    for (const DisplayDevice& device : devices) {
        const DriverFiles files = DriverRegistry::read(device);
        if (files.umd_own.empty()) {
            problems.add(device.name + ": UserModeDriverName" + (sizeof(void*) == 4 ? "Wow" : "") +
                         " is empty");
        }
        if (files.umd_other.empty()) {
            problems.add(device.name + ": UserModeDriverName" + (sizeof(void*) == 8 ? "Wow" : "") +
                         " is empty");
        }
        for (const std::wstring& path : files.umd_other) {
            if (!DriverRegistry::exists(path)) {
                problems.add(narrow(path) + " (other bitness) does not exist");
            }
        }
        for (const std::wstring& path : files.umd_own) {
            std::string error;
            if (!DriverRegistry::exists(path)) {
                problems.add(narrow(path) + " does not exist");
            } else if (!loads(path, error)) {
                problems.add(error);
            }
        }
    }
    return problems.outcome();
}

Outcome InstallChecks::signed_and_loadable(Context& context) {
    const auto begin = std::chrono::steady_clock::now();
    const std::vector<DisplayDevice> devices = accel_devices();
    if (devices.empty()) {
        return Outcome::fail("no accel-virt display device in SetupAPI");
    }
    Problems problems;
    for (const DisplayDevice& device : devices) {
        if (device.problem_code == kProblemUnsignedDriver) {
            problems.add(device.name + ": the OS rejected the driver as unsigned (problem code 52)");
        }
        for (const std::wstring& path : DriverRegistry::read(device).umd_own) {
            std::string error;
            if (!loads(path, error)) {
                problems.add(error);
            }
        }
    }
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::accel(adapters, error)) {
        problems.add(error);
    }
    for (const AvAdapter& adapter : adapters) {
        if (!adapter.umd_version_known) {
            problems.add("querying " + adapter.name + " for its driver fails");
        }
    }
    const long long elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin)
            .count();
    const EventLogHits hits = EventLogScan::recent(L"Microsoft-Windows-CodeIntegrity/Operational",
                                                   {3001, 3002, 3003, 3004, 3033, 3034, 3077},
                                                   elapsed + 2000);
    context.measure("codeintegrity.channel", hits.channel_available);
    for (const std::string& event : hits.events) {
        problems.add("code integrity event: " + event);
    }
    return problems.outcome();
}

Outcome InstallChecks::no_duplicate_adapters(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    KmtApi kmt;
    std::vector<KmtAdapter> kmt_adapters;
    VulkanApi vulkan;
    std::vector<VulkanDevice> vk_devices;
    if (!kmt.load(error) || !kmt.enum_adapters(kmt_adapters, error) || !vulkan.load(error) ||
        !vulkan.create_instance(error) || !vulkan.devices(vk_devices, error)) {
        return Outcome::fail(error);
    }
    std::size_t devices = 0;
    for (const DisplayDevice& device : DisplayDevices::enumerate(true)) {
        devices += device.accel ? std::size_t{1} : std::size_t{0};
    }
    Problems problems;
    std::set<std::uint64_t> accel_luids;
    std::size_t accel_count = 0;
    for (const AvAdapter& adapter : adapters) {
        if (!adapter.accel()) {
            context.measure("other.dxgi." + adapter.luid_text(), adapter.name);
            continue;
        }
        ++accel_count;
        if (!accel_luids.insert(adapter.luid_key()).second) {
            problems.add("DXGI lists accel-virt LUID " + adapter.luid_text() + " twice");
        }
    }
    if (accel_count == 0) {
        problems.add("DXGI lists no accel-virt adapter");
    }
    if (accel_count != devices) {
        problems.add(std::to_string(accel_count) + " accel-virt DXGI adapter(s) but " + std::to_string(devices) +
                     " present accel-virt display device(s) in the device tree");
    }
    for (std::uint64_t key : accel_luids) {
        std::size_t in_kmt = 0;
        for (const KmtAdapter& adapter : kmt_adapters) {
            in_kmt += AvAdapters::key(adapter.luid) == key ? std::size_t{1} : std::size_t{0};
        }
        std::size_t in_vulkan = 0;
        for (const VulkanDevice& device : vk_devices) {
            in_vulkan += device.luid_valid && AvAdapters::key(device.luid) == key ? std::size_t{1} : std::size_t{0};
        }
        const std::string luid = Text::hex(key >> 32, 8) + ":" + Text::hex(key & 0xffffffffu, 8);
        if (in_kmt != 1) {
            problems.add("accel-virt LUID " + luid + " appears " + std::to_string(in_kmt) + " times in D3DKMT");
        }
        if (in_vulkan != 1) {
            problems.add("accel-virt LUID " + luid + " appears " + std::to_string(in_vulkan) + " times in Vulkan");
        }
    }
    for (const KmtAdapter& adapter : kmt_adapters) {
        if (accel_luids.count(AvAdapters::key(adapter.luid)) == 0) {
            context.measure("other.kmt." + Text::hex(static_cast<std::uint32_t>(adapter.luid.HighPart), 8) + ":" +
                                Text::hex(adapter.luid.LowPart, 8),
                            "not accel-virt");
        }
    }
    context.measure("accel.dxgi", accel_count);
    context.measure("accel.setupapi", devices);
    return problems.outcome();
}

Outcome InstallChecks::requirements_hold(Context& context) {
    constexpr REGSAM view = RegistryReader::kView64;
    Problems problems;
    if (RegistryReader::dword(L"SOFTWARE\\Microsoft\\Windows\\Dwm", L"DisableDDisplay", view)
            .value_or(0) != 1) {
        problems.add("DWM DisableDDisplay is not 1 (DisplayCore path not disabled)");
    }
    bool known = false;
    const bool display_core = dwm_has_display_core(known);
    context.measure("dwm.display_core_loaded", known ? Json(display_core) : Json("unknown"));
    if (known && display_core) {
        problems.add("dwm.exe in the console session has ddisplay.dll loaded");
    }
    const std::wstring options =
        Text::upper(RegistryReader::string(L"SYSTEM\\CurrentControlSet\\Control", L"SystemStartOptions",
                                           view)
                        .value_or(L""));
    if (options.find(L"TESTSIGNING") == std::wstring::npos) {
        problems.add("SystemStartOptions does not contain TESTSIGNING");
    }
    const std::optional<DWORD> integrity = code_integrity_options();
    if (!integrity) {
        problems.add("NtQuerySystemInformation(SystemCodeIntegrityInformation) fails");
    } else if ((*integrity & 0x2) == 0) {
        problems.add("the running kernel does not have test signing enabled");
    }
    if (RegistryReader::dword(L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate",
                              L"ExcludeWUDriversInQualityUpdate", view)
            .value_or(0) != 1) {
        problems.add("ExcludeWUDriversInQualityUpdate is not 1");
    }
    if (RegistryReader::dword(L"SOFTWARE\\Policies\\Microsoft\\Windows\\DriverSearching",
                              L"DontSearchWindowsUpdate", view)
            .value_or(0) != 1) {
        problems.add("DontSearchWindowsUpdate is not 1");
    }
    if (RegistryReader::dword(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DriverSearching",
                              L"SearchOrderConfig", view)
            .value_or(1) != 0) {
        problems.add("SearchOrderConfig is not 0");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::env
