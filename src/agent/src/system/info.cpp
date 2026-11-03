#include "lsw/agent/system/info.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/log.hpp"

#include "lsw_build_stamp.h"

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <sysinfoapi.h>
#include "lsw/agent/win/file_time.hpp"
// clang-format on

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace lsw::agent::system {

namespace {
std::atomic<bool> g_cpu_baseline_valid{false};
std::atomic<std::uint64_t> g_cpu_last_idle_100ns{0};
std::atomic<std::uint64_t> g_cpu_last_kernel_100ns{0};
std::atomic<std::uint64_t> g_cpu_last_user_100ns{0};

[[nodiscard]] int sample_cpu_percent() {
    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (::GetSystemTimes(&idle, &kernel, &user) == 0) {
        return 0;
    }
    const auto idle_now = win::FileTime::to_u64(idle);
    const auto kernel_now = win::FileTime::to_u64(kernel);
    const auto user_now = win::FileTime::to_u64(user);

    int pct = 0;
    if (g_cpu_baseline_valid.load(std::memory_order_acquire)) {
        const auto idle_prev = g_cpu_last_idle_100ns.load(std::memory_order_relaxed);
        const auto kernel_prev = g_cpu_last_kernel_100ns.load(std::memory_order_relaxed);
        const auto user_prev = g_cpu_last_user_100ns.load(std::memory_order_relaxed);
        const std::uint64_t total = (kernel_now + user_now) - (kernel_prev + user_prev);
        const std::uint64_t idle_delta = idle_now - idle_prev;
        if (total > 0 && total >= idle_delta) {
            const std::uint64_t busy = total - idle_delta;
            pct = static_cast<int>((busy * 100ULL) / total);
            if (pct > 100)
                pct = 100;
            if (pct < 0)
                pct = 0;
        }
    }
    g_cpu_last_idle_100ns.store(idle_now, std::memory_order_relaxed);
    g_cpu_last_kernel_100ns.store(kernel_now, std::memory_order_relaxed);
    g_cpu_last_user_100ns.store(user_now, std::memory_order_relaxed);
    g_cpu_baseline_valid.store(true, std::memory_order_release);
    return pct;
}

std::string get_hostname() {
    char buf[256] = {};
    DWORD n = sizeof(buf);
    ::GetComputerNameA(buf, &n);
    return std::string{buf, n};
}

std::string get_primary_ipv4() {
    ULONG sz = 16384;
    auto* addrs = static_cast<IP_ADAPTER_ADDRESSES*>(::HeapAlloc(::GetProcessHeap(), 0, sz));
    if (addrs == nullptr) {
        return {};
    }
    if (::GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr,
                               addrs, &sz) != NO_ERROR) {
        ::HeapFree(::GetProcessHeap(), 0, addrs);
        return {};
    }
    std::string out;
    for (auto* a = addrs; a != nullptr; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next) {
            char buf[64] = {};
            sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            ::inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
            out = buf;
            break;
        }
        if (!out.empty())
            break;
    }
    ::HeapFree(::GetProcessHeap(), 0, addrs);
    return out;
}

nlohmann::json read_json_file(const std::wstring& path) {
    std::ifstream file{std::filesystem::path{path}, std::ios::binary};
    if (!file) {
        return nullptr;
    }
    return nlohmann::json::parse(file, nullptr, false);
}

}

const char* Info::build_stamp() {
    return LSW_BUILD_STAMP;
}

nlohmann::json Info::release_record() {
    return read_json_file(core::Paths::release_file());
}

nlohmann::json Info::collect() {
    LSW_AGENT_TRACE("system_info: collecting");
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    ::GlobalMemoryStatusEx(&ms);
    OSVERSIONINFOA osv{};
    osv.dwOSVersionInfoSize = sizeof(osv);
#pragma warning(suppress : 4996)
    ::GetVersionExA(&osv);

    ULARGE_INTEGER disk_free{};
    ULARGE_INTEGER disk_total{};
    if (::GetDiskFreeSpaceExW(L"C:\\", nullptr, &disk_total, &disk_free) == 0) {
        disk_free.QuadPart = 0;
        disk_total.QuadPart = 0;
    }

    const int cpu_pct = sample_cpu_percent();

    nlohmann::json j = nlohmann::json::object();
    j["os"] = "Windows " + std::to_string(osv.dwMajorVersion) + "." +
              std::to_string(osv.dwMinorVersion) + "." + std::to_string(osv.dwBuildNumber);
    const auto hostname = get_hostname();
    const auto ipv4 = get_primary_ipv4();
    j["hostname"] = hostname;
    j["ipv4"] = ipv4;
    j["mem_total"] = static_cast<std::uint64_t>(ms.ullTotalPhys);
    j["mem_avail"] = static_cast<std::uint64_t>(ms.ullAvailPhys);
    j["cpu_percent"] = cpu_pct;
    j["disk_total"] = static_cast<std::uint64_t>(disk_total.QuadPart);
    j["disk_avail"] = static_cast<std::uint64_t>(disk_free.QuadPart);
    j["agent_version"] = LSW_BUILD_STAMP;
    const auto release = read_json_file(core::Paths::release_file());
    j["release"] = release.is_object() && release.value("release", nlohmann::json{}).is_string()
                       ? release["release"].get<std::string>()
                       : std::string{};
    j["release_config"] = release.is_object() && release.value("config", nlohmann::json{}).is_string()
                              ? release["config"].get<std::string>()
                              : std::string{};
    j["release_version"] = release.is_object() && release.value("version", nlohmann::json{}).is_string()
                               ? release["version"].get<std::string>()
                               : std::string{};
    const auto last_update = read_json_file(core::Paths::update_result_file());
    j["last_update"] = last_update.is_object() ? last_update : nlohmann::json(nullptr);
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE) {
            j["agent_started"] = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) |
                                 created.dwLowDateTime;
        }
    }
    LSW_AGENT_TRACE("system_info: hostname={} ipv4={} mem_total={}MiB mem_avail={}MiB cpu={}% "
                   "disk_total={}GiB disk_avail={}GiB",
                   hostname, ipv4, ms.ullTotalPhys >> 20, ms.ullAvailPhys >> 20, cpu_pct,
                   disk_total.QuadPart >> 30, disk_free.QuadPart >> 30);
    return j;
}

}
