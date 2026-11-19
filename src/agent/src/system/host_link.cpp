#include "lsw/agent/system/host_link.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/install/process_runner.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/powershell_command.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/bstr.hpp"
#include "lsw/agent/win/com.hpp"
#include "lsw/agent/win/com_ptr.hpp"
#include "lsw/agent/win/reg_key.hpp"

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <netioapi.h>
#include <objbase.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <netfw.h>
// clang-format on

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lsw::agent::system {

namespace {
using Clock = std::chrono::steady_clock;
using util::TextEncoding;

constexpr const wchar_t* kLocationPath = L"PCIROOT(0)#PCI(1000)";
constexpr std::array<const wchar_t*, 3> kHardwareIds{
    L"PCI\\VEN_1AF4&DEV_1000",
    L"PCI\\VEN_1AF4&DEV_1041",
    L"PCI\\VEN_8086&DEV_10D3",
};
constexpr const wchar_t* kName = L"LSW host link";
constexpr const wchar_t* kStateKey = L"Software\\LSW\\HostLink";
constexpr const wchar_t* kNamedValue = L"NamedInterface";
constexpr ULONG kMetric = 500;
constexpr std::uint32_t kHostAddress = 0x0A000202U;
constexpr std::uint32_t kSubnetMask = 0xFFFFFF00U;
constexpr auto kBackstop = std::chrono::seconds{60};
constexpr DWORD kDebounceMs = 2000;
constexpr unsigned long kScriptTimeoutMs = 2UL * 60UL * 1000UL;
constexpr unsigned long kDriverTimeoutMs = 5UL * 60UL * 1000UL;

struct FirewallRule {
    const wchar_t* name;
    const wchar_t* port;
};

constexpr std::array<FirewallRule, 2> kFirewallRules{
    FirewallRule{L"LSW Agent", L"5102"},
    FirewallRule{L"LSW Stream", L"5103"},
};

struct DeviceInfoList {
    HDEVINFO set = INVALID_HANDLE_VALUE;

    explicit DeviceInfoList(HDEVINFO s) noexcept : set(s) {}

    ~DeviceInfoList() {
        if (set != INVALID_HANDLE_VALUE) {
            ::SetupDiDestroyDeviceInfoList(set);
        }
    }

    DeviceInfoList(const DeviceInfoList&) = delete;
    DeviceInfoList& operator=(const DeviceInfoList&) = delete;

    [[nodiscard]] bool ok() const noexcept { return set != INVALID_HANDLE_VALUE; }
};

struct Seen {
    bool readable = true;
    HostLinkState state;
    std::wstring instance;
    std::wstring location;
    std::vector<std::wstring> ghosts;
    bool virtio = false;
    std::wstring interface_guid;
    NET_LUID luid{};
    bool has_luid = false;
    std::wstring friendly_name;
    ULONG problem = 0;
};

std::vector<std::wstring> multi_property(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    DWORD type = 0;
    DWORD needed = 0;
    ::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, nullptr, 0, &needed);
    if (needed < sizeof(wchar_t)) {
        return {};
    }
    std::vector<wchar_t> buffer(needed / sizeof(wchar_t) + 2, L'\0');
    if (::SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type,
                                            reinterpret_cast<BYTE*>(buffer.data()), needed,
                                            nullptr) == FALSE) {
        return {};
    }
    std::vector<std::wstring> out;
    for (const wchar_t* at = buffer.data(); *at != L'\0'; at += std::wcslen(at) + 1) {
        out.emplace_back(at);
        if (type != REG_MULTI_SZ) {
            break;
        }
    }
    return out;
}

std::wstring string_property(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    auto values = multi_property(set, device, property);
    return values.empty() ? std::wstring{} : values.front();
}

std::wstring instance_id(HDEVINFO set, SP_DEVINFO_DATA& device) {
    DWORD needed = 0;
    ::SetupDiGetDeviceInstanceIdW(set, &device, nullptr, 0, &needed);
    std::wstring id(needed, L'\0');
    if (needed == 0 ||
        ::SetupDiGetDeviceInstanceIdW(set, &device, id.data(), needed, nullptr) == FALSE) {
        return {};
    }
    id.resize(std::wcslen(id.c_str()));
    return id;
}

bool our_hardware(const std::vector<std::wstring>& ids, bool& virtio) {
    for (const std::wstring& id : ids) {
        for (const wchar_t* ours : kHardwareIds) {
            if (::_wcsnicmp(id.c_str(), ours, std::wcslen(ours)) == 0) {
                virtio = ::_wcsnicmp(ours, L"PCI\\VEN_1AF4", 12) == 0;
                return true;
            }
        }
    }
    return false;
}

std::wstring net_cfg_instance(HDEVINFO set, SP_DEVINFO_DATA& device) {
    const HKEY raw = ::SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    if (raw == INVALID_HANDLE_VALUE || raw == nullptr) {
        return {};
    }
    const win::UniqueRegKey key{raw};
    wchar_t value[64]{};
    DWORD size = sizeof(value);
    if (::RegGetValueW(key.get(), nullptr, L"NetCfgInstanceId", RRF_RT_REG_SZ, nullptr, value,
                       &size) != ERROR_SUCCESS) {
        return {};
    }
    return value;
}

void observe_device(Seen& seen) {
    const DeviceInfoList list{::SetupDiGetClassDevsW(nullptr, L"PCI", nullptr, DIGCF_ALLCLASSES)};
    if (!list.ok()) {
        seen.readable = false;
        LSW_AGENT_WARN("host link: cannot list PCI devices (error {})", ::GetLastError());
        return;
    }

    struct Candidate {
        std::wstring instance;
        std::wstring location;
    };

    std::vector<Candidate> absent;
    bool found = false;
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; ::SetupDiEnumDeviceInfo(list.set, index, &device) != FALSE; ++index) {
        bool virtio = false;
        if (!our_hardware(multi_property(list.set, device, SPDRP_HARDWAREID), virtio)) {
            continue;
        }
        ULONG status = 0;
        ULONG problem = 0;
        const bool present =
            ::CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0) == CR_SUCCESS;
        if (!present) {
            absent.push_back({instance_id(list.set, device),
                              string_property(list.set, device, SPDRP_LOCATION_INFORMATION)});
            continue;
        }
        const auto paths = multi_property(list.set, device, SPDRP_LOCATION_PATHS);
        if (std::find(paths.begin(), paths.end(), std::wstring{kLocationPath}) == paths.end()) {
            continue;
        }
        found = true;
        seen.instance = instance_id(list.set, device);
        seen.location = string_property(list.set, device, SPDRP_LOCATION_INFORMATION);
        seen.virtio = virtio;
        seen.problem = problem;
        if ((status & DN_HAS_PROBLEM) != 0) {
            if (problem == CM_PROB_DISABLED) {
                seen.state.device = HostLinkState::Device::Disabled;
            } else if (problem == CM_PROB_NOT_CONFIGURED || problem == CM_PROB_FAILED_INSTALL ||
                       problem == CM_PROB_REINSTALL) {
                seen.state.device = HostLinkState::Device::NoDriver;
            } else {
                seen.state.device = HostLinkState::Device::Failing;
            }
        } else if ((status & DN_STARTED) == 0) {
            seen.state.device = HostLinkState::Device::Failing;
        } else {
            seen.state.device = HostLinkState::Device::Working;
            seen.interface_guid = net_cfg_instance(list.set, device);
        }
    }
    if (!found) {
        seen.state.device = HostLinkState::Device::Absent;
        return;
    }
    for (const Candidate& ghost : absent) {
        if (!seen.location.empty() && ghost.location == seen.location && !ghost.instance.empty() &&
            ghost.instance != seen.instance) {
            seen.ghosts.push_back(ghost.instance);
        }
    }
    seen.state.ghosts = !seen.ghosts.empty();
}

bool metric_ours(const MIB_IPINTERFACE_ROW& row) {
    return row.UseAutomaticMetric == FALSE && row.Metric == kMetric;
}

void observe_interface(Seen& seen) {
    GUID guid{};
    if (seen.interface_guid.empty() ||
        FAILED(::CLSIDFromString(seen.interface_guid.c_str(), &guid)) ||
        ::ConvertInterfaceGuidToLuid(&guid, &seen.luid) != NO_ERROR) {
        seen.state.ipv4_bound = false;
        return;
    }
    seen.has_luid = true;

    MIB_IPINTERFACE_ROW v4{};
    ::InitializeIpInterfaceEntry(&v4);
    v4.Family = AF_INET;
    v4.InterfaceLuid = seen.luid;
    seen.state.ipv4_bound = ::GetIpInterfaceEntry(&v4) == NO_ERROR;
    seen.state.metric_v4 = seen.state.ipv4_bound && metric_ours(v4);

    MIB_IPINTERFACE_ROW v6{};
    ::InitializeIpInterfaceEntry(&v6);
    v6.Family = AF_INET6;
    v6.InterfaceLuid = seen.luid;
    seen.state.metric_v6 = ::GetIpInterfaceEntry(&v6) != NO_ERROR || metric_ours(v6);

    ULONG size = 16U * 1024U;
    std::vector<std::uint8_t> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && result == ERROR_BUFFER_OVERFLOW; ++tries) {
        buffer.resize(size);
        result = ::GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result == NO_ERROR) {
        for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
             adapter != nullptr; adapter = adapter->Next) {
            if (adapter->Luid.Value != seen.luid.Value) {
                continue;
            }
            seen.friendly_name = adapter->FriendlyName != nullptr ? adapter->FriendlyName : L"";
            seen.state.dhcp = (adapter->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
                 unicast = unicast->Next) {
                const sockaddr* address = unicast->Address.lpSockaddr;
                if (address == nullptr || address->sa_family != AF_INET ||
                    unicast->DadState != IpDadStatePreferred) {
                    continue;
                }
                const auto* in = reinterpret_cast<const sockaddr_in*>(address);
                if ((ntohl(in->sin_addr.s_addr) & kSubnetMask) == (kHostAddress & kSubnetMask)) {
                    seen.state.has_address = true;
                }
            }
        }
    }

    MIB_IPFORWARD_ROW2 route{};
    ::InitializeIpForwardEntry(&route);
    route.InterfaceLuid = seen.luid;
    route.DestinationPrefix.Prefix.si_family = AF_INET;
    route.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
    route.DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr = htonl(kHostAddress);
    route.DestinationPrefix.PrefixLength = 32;
    route.NextHop.si_family = AF_INET;
    route.NextHop.Ipv4.sin_family = AF_INET;
    seen.state.host_route = ::GetIpForwardEntry2(&route) == NO_ERROR;

    const auto key = win::RegKey::open(HKEY_LOCAL_MACHINE, kStateKey, KEY_READ);
    const auto named = key.has_value() ? key->read_string(kNamedValue) : std::nullopt;
    seen.state.named =
        seen.friendly_name == kName || (named.has_value() && *named == seen.interface_guid);
}

bool firewall_rule_ok(INetFwRules* rules, const FirewallRule& wanted) {
    win::ComPtr<INetFwRule> rule;
    const win::Bstr name = win::Bstr::from_utf8(TextEncoding::to_utf8(wanted.name));
    if (FAILED(rules->Item(name.get(), rule.GetAddressOf())) || !rule) {
        return false;
    }
    VARIANT_BOOL enabled = VARIANT_FALSE;
    NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_OUT;
    NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
    LONG protocol = 0;
    LONG profiles = 0;
    win::Bstr ports;
    if (FAILED(rule->get_Enabled(&enabled)) || FAILED(rule->get_Direction(&direction)) ||
        FAILED(rule->get_Action(&action)) || FAILED(rule->get_Protocol(&protocol)) ||
        FAILED(rule->get_Profiles(&profiles)) || FAILED(rule->get_LocalPorts(ports.put()))) {
        return false;
    }
    const long all = NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC;
    return enabled != VARIANT_FALSE && direction == NET_FW_RULE_DIR_IN &&
           action == NET_FW_ACTION_ALLOW && protocol == NET_FW_IP_PROTOCOL_TCP &&
           (profiles & all) == all && ports.get() != nullptr &&
           std::wstring{ports.get()} == wanted.port;
}

void observe_firewall(Seen& seen) {
    win::ComPtr<INetFwPolicy2> policy;
    if (FAILED(::CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(INetFwPolicy2),
                                  reinterpret_cast<void**>(policy.GetAddressOf())))) {
        return;
    }
    win::ComPtr<INetFwRules> rules;
    if (FAILED(policy->get_Rules(rules.GetAddressOf())) || !rules) {
        return;
    }
    for (const FirewallRule& rule : kFirewallRules) {
        if (!firewall_rule_ok(rules.Get(), rule)) {
            seen.state.firewall = false;
        }
    }
}

Seen observe() {
    Seen seen;
    observe_firewall(seen);
    observe_device(seen);
    if (seen.state.device == HostLinkState::Device::Working) {
        observe_interface(seen);
    }
    return seen;
}

bool run_quiet(const std::wstring& command, unsigned long timeout_ms, const char* what,
               int* exit_code = nullptr) {
    const auto outcome = install::ProcessRunner::run(command, timeout_ms);
    if (exit_code != nullptr) {
        *exit_code = outcome.exit_code;
    }
    if (!outcome.completed) {
        LSW_AGENT_WARN("host link: {} did not finish: {}", what, outcome.error);
        return false;
    }
    if (outcome.exit_code != 0 && exit_code == nullptr) {
        LSW_AGENT_WARN("host link: {} exited {}", what, outcome.exit_code);
        return false;
    }
    return true;
}

std::wstring system_tool(const wchar_t* exe) {
    wchar_t dir[MAX_PATH]{};
    const UINT length = ::GetSystemDirectoryW(dir, MAX_PATH);
    return (length == 0 || length >= MAX_PATH ? std::wstring{L"C:\\Windows\\System32"}
                                              : std::wstring{dir, length}) +
           L"\\" + exe;
}

bool run_script(const std::string& body, const std::wstring& interface_guid, const char* what) {
    std::string script =
        "$ErrorActionPreference = 'Stop'\n"
        "$a = Get-NetAdapter -IncludeHidden | Where-Object { $_.InterfaceGuid -eq '" +
        TextEncoding::to_utf8(interface_guid) +
        "' }\n"
        "if (-not $a) { exit 3 }\n" +
        body + "\nexit 0\n";
    return run_quiet(util::PowerShellCommand::encoded(script), kScriptTimeoutMs, what);
}

bool rescan() {
    DEVINST root = 0;
    if (::CM_Locate_DevNodeW(&root, nullptr, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return false;
    }
    return ::CM_Reenumerate_DevNode(root, CM_REENUMERATE_RETRY_INSTALLATION) == CR_SUCCESS;
}

template<class Params>
bool call_installer(const std::wstring& instance, DI_FUNCTION function, Params params,
                    const char* what) {
    const DeviceInfoList list{::SetupDiCreateDeviceInfoList(nullptr, nullptr)};
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    if (!list.ok() ||
        ::SetupDiOpenDeviceInfoW(list.set, instance.c_str(), nullptr, 0, &device) == FALSE) {
        LSW_AGENT_WARN("host link: cannot open {} to {} (error {})",
                       TextEncoding::to_utf8(instance), what, ::GetLastError());
        return false;
    }
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = function;
    if (::SetupDiSetClassInstallParamsW(list.set, &device, &params.ClassInstallHeader,
                                        sizeof(params)) == FALSE ||
        ::SetupDiCallClassInstaller(function, list.set, &device) == FALSE) {
        LSW_AGENT_WARN("host link: could not {} {} (error {})", what,
                       TextEncoding::to_utf8(instance), ::GetLastError());
        return false;
    }
    SP_DEVINSTALL_PARAMS_W install{};
    install.cbSize = sizeof(install);
    if (::SetupDiGetDeviceInstallParamsW(list.set, &device, &install) != FALSE &&
        (install.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0) {
        LSW_AGENT_WARN("host link: Windows needs a restart to {}", what);
    }
    return true;
}

bool enable(const Seen& seen) {
    SP_PROPCHANGE_PARAMS params{};
    params.StateChange = DICS_ENABLE;
    params.Scope = DICS_FLAG_GLOBAL;
    params.HwProfile = 0;
    return call_installer(seen.instance, DIF_PROPERTYCHANGE, params, "enable");
}

bool install_driver(const Seen& seen) {
    const std::wstring inf = core::Paths::root() + L"\\network\\netkvm.inf";
    std::error_code ignored;
    if (seen.virtio && std::filesystem::exists(inf, ignored)) {
        int code = 0;
        if (run_quiet(system_tool(L"pnputil.exe") + L" /add-driver \"" + inf + L"\" /install",
                      kDriverTimeoutMs, "pnputil netkvm", &code) &&
            code != 0 && code != 259 && code != 3010 && code != 1641) {
            LSW_AGENT_WARN("host link: pnputil netkvm exited {}", code);
        }
    }
    return rescan();
}

bool remove_ghosts(const Seen& seen) {
    bool all = true;
    for (const std::wstring& ghost : seen.ghosts) {
        SP_REMOVEDEVICE_PARAMS params{};
        params.Scope = DI_REMOVEDEVICE_GLOBAL;
        params.HwProfile = 0;
        if (call_installer(ghost, DIF_REMOVE, params, "remove")) {
            LSW_AGENT_INFO("host link: removed {}, an adapter no longer present at its slot",
                           TextEncoding::to_utf8(ghost));
        } else {
            all = false;
        }
    }
    return all;
}

bool set_metric(const Seen& seen) {
    bool ok = true;
    for (const ADDRESS_FAMILY family :
         {static_cast<ADDRESS_FAMILY>(AF_INET), static_cast<ADDRESS_FAMILY>(AF_INET6)}) {
        MIB_IPINTERFACE_ROW row{};
        ::InitializeIpInterfaceEntry(&row);
        row.Family = family;
        row.InterfaceLuid = seen.luid;
        if (::GetIpInterfaceEntry(&row) != NO_ERROR || metric_ours(row)) {
            continue;
        }
        row.UseAutomaticMetric = FALSE;
        row.Metric = kMetric;
        if (family == AF_INET) {
            row.SitePrefixLength = 0;
        }
        const DWORD result = ::SetIpInterfaceEntry(&row);
        if (result != NO_ERROR) {
            LSW_AGENT_WARN("host link: setting the {} metric failed (error {})",
                           family == AF_INET ? "IPv4" : "IPv6", result);
            ok = false;
        }
    }
    return ok;
}

bool add_host_route(const Seen& seen) {
    MIB_IPFORWARD_ROW2 route{};
    ::InitializeIpForwardEntry(&route);
    route.InterfaceLuid = seen.luid;
    route.DestinationPrefix.Prefix.si_family = AF_INET;
    route.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
    route.DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr = htonl(kHostAddress);
    route.DestinationPrefix.PrefixLength = 32;
    route.NextHop.si_family = AF_INET;
    route.NextHop.Ipv4.sin_family = AF_INET;
    route.Metric = 0;
    route.Protocol = static_cast<NL_ROUTE_PROTOCOL>(MIB_IPPROTO_NETMGMT);
    const DWORD result = ::CreateIpForwardEntry2(&route);
    if (result != NO_ERROR && result != ERROR_OBJECT_ALREADY_EXISTS) {
        LSW_AGENT_WARN("host link: adding the route to 10.0.2.2 failed (error {})", result);
        return false;
    }
    return true;
}

bool name(const Seen& seen) {
    if (seen.friendly_name != kName) {
        if (seen.friendly_name.empty()) {
            return false;
        }
        if (!run_quiet(system_tool(L"netsh.exe") + L" interface set interface name=\"" +
                           seen.friendly_name + L"\" newname=\"" + kName + L"\"",
                       kScriptTimeoutMs, "renaming the adapter")) {
            return false;
        }
        LSW_AGENT_INFO("host link: renamed '{}' to '{}'", TextEncoding::to_utf8(seen.friendly_name),
                       TextEncoding::to_utf8(kName));
    }
    const auto key = win::RegKey::create(HKEY_LOCAL_MACHINE, kStateKey, KEY_WRITE);
    return key.has_value() && key->write_string(kNamedValue, seen.interface_guid) == ERROR_SUCCESS;
}

bool restore_firewall() {
    bool ok = true;
    for (const FirewallRule& rule : kFirewallRules) {
        const std::wstring netsh = system_tool(L"netsh.exe");
        int ignored = 0;
        (void)run_quiet(netsh + L" advfirewall firewall delete rule \"name=" + rule.name + L"\"",
                        kScriptTimeoutMs, "deleting a firewall rule", &ignored);
        ok = run_quiet(netsh + L" advfirewall firewall add rule \"name=" + rule.name +
                           L"\" dir=in action=allow protocol=TCP localport=" + rule.port +
                           L" profile=any",
                       kScriptTimeoutMs, "adding a firewall rule") &&
             ok;
    }
    return ok;
}

bool apply(HostLinkRepair repair, const Seen& seen) {
    switch (repair) {
    case HostLinkRepair::Rescan:
        return rescan();
    case HostLinkRepair::Enable:
        return enable(seen);
    case HostLinkRepair::InstallDriver:
        return install_driver(seen);
    case HostLinkRepair::RemoveGhosts:
        return remove_ghosts(seen);
    case HostLinkRepair::BindIpv4:
        return run_script("Disable-NetAdapterBinding -Name $a.Name -IncludeHidden -ComponentID "
                          "ms_bridge -ErrorAction SilentlyContinue\n"
                          "Enable-NetAdapterBinding -Name $a.Name -IncludeHidden -ComponentID "
                          "ms_tcpip",
                          seen.interface_guid, "binding IPv4");
    case HostLinkRepair::RestoreDhcp:
        return run_script(
            "$i = $a.ifIndex\n"
            "Remove-NetRoute -InterfaceIndex $i -AddressFamily IPv4 -Confirm:$false "
            "-ErrorAction SilentlyContinue\n"
            "Remove-NetIPAddress -InterfaceIndex $i -AddressFamily IPv4 "
            "-Confirm:$false -ErrorAction SilentlyContinue\n"
            "Set-NetIPInterface -InterfaceIndex $i -AddressFamily IPv4 -Dhcp Enabled\n"
            "Set-DnsClientServerAddress -InterfaceIndex $i -ResetServerAddresses",
            seen.interface_guid, "restoring DHCP");
    case HostLinkRepair::Metric:
        return set_metric(seen);
    case HostLinkRepair::HostRoute:
        return add_host_route(seen);
    case HostLinkRepair::Name:
        return name(seen);
    case HostLinkRepair::Firewall:
        return restore_firewall();
    case HostLinkRepair::Count:
        break;
    }
    return false;
}

const char* device_text(HostLinkState::Device device) {
    switch (device) {
    case HostLinkState::Device::Absent:
        return "missing";
    case HostLinkState::Device::Working:
        return "working";
    case HostLinkState::Device::Disabled:
        return "disabled";
    case HostLinkState::Device::NoDriver:
        return "without a driver";
    case HostLinkState::Device::Failing:
        return "not starting";
    }
    return "?";
}

std::string list_text(const std::vector<HostLinkRepair>& repairs) {
    std::string out;
    for (const HostLinkRepair repair : repairs) {
        if (!out.empty()) {
            out += ", ";
        }
        out += HostLinkPlan::name(repair);
    }
    return out;
}

void WINAPI on_interface(PVOID wake, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
    ::SetEvent(static_cast<HANDLE>(wake));
}

void WINAPI on_address(PVOID wake, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) {
    ::SetEvent(static_cast<HANDLE>(wake));
}

void WINAPI on_route(PVOID wake, PMIB_IPFORWARD_ROW2, MIB_NOTIFICATION_TYPE) {
    ::SetEvent(static_cast<HANDLE>(wake));
}

}  // namespace

HostLink::HostLink()
    : stop_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      wake_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

HostLink::~HostLink() {
    stop();
}

void HostLink::start() {
    if (thread_.joinable() || !stop_ || !wake_) {
        return;
    }
    HANDLE handle = nullptr;
    if (::NotifyIpInterfaceChange(AF_UNSPEC, on_interface, wake_.get(), FALSE, &handle) ==
        NO_ERROR) {
        notifications_.push_back(handle);
    }
    if (::NotifyUnicastIpAddressChange(AF_INET, on_address, wake_.get(), FALSE, &handle) ==
        NO_ERROR) {
        notifications_.push_back(handle);
    }
    if (::NotifyRouteChange2(AF_INET, on_route, wake_.get(), FALSE, &handle) == NO_ERROR) {
        notifications_.push_back(handle);
    }
    if (notifications_.size() < 3) {
        LSW_AGENT_WARN("host link: only {} of 3 network notifications registered; checking every "
                       "{} s",
                       notifications_.size(), kBackstop.count());
    }
    thread_ = std::thread([this] { run(); });
}

void HostLink::stop() {
    for (HANDLE handle : notifications_) {
        ::CancelMibChangeNotify2(handle);
    }
    notifications_.clear();
    if (stop_) {
        ::SetEvent(stop_.get());
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HostLink::run() {
    const win::Apartment apartment;
    RepairBackoff backoff;
    for (;;) {
        ::ResetEvent(wake_.get());
        try {
            check(backoff);
        } catch (const std::exception& e) {
            LSW_AGENT_WARN("host link: check failed: {}", e.what());
        }
        auto wait = kBackstop;
        const auto now = Clock::now();
        if (const auto due = backoff.next_due(); due.has_value()) {
            const auto until = std::chrono::ceil<std::chrono::seconds>(*due - now);
            wait = std::clamp(until, std::chrono::seconds{1}, kBackstop);
        }
        const std::array<HANDLE, 2> handles{stop_.get(), wake_.get()};
        const DWORD woke = ::WaitForMultipleObjects(
            static_cast<DWORD>(handles.size()), handles.data(), FALSE,
            static_cast<DWORD>(
                std::chrono::duration_cast<std::chrono::milliseconds>(wait).count()));
        if (woke == WAIT_OBJECT_0) {
            return;
        }
        if (woke == WAIT_OBJECT_0 + 1 &&
            ::WaitForSingleObject(stop_.get(), kDebounceMs) == WAIT_OBJECT_0) {
            return;
        }
    }
}

void HostLink::check(RepairBackoff& backoff) {
    const Seen seen = observe();
    if (!seen.readable) {
        return;
    }
    const auto needed = HostLinkPlan::repairs(seen.state);
    const auto now = Clock::now();
    backoff.observe(needed, now);
    if (!reported_) {
        reported_ = true;
        LSW_AGENT_INFO("host link: {} '{}' {}, {}", TextEncoding::to_utf8(seen.instance),
                       TextEncoding::to_utf8(seen.friendly_name), device_text(seen.state.device),
                       seen.state.has_address ? "has its address" : "has no address yet");
    }
    if (needed != last_needed_) {
        if (needed.empty()) {
            LSW_AGENT_INFO("host link: in order");
        } else {
            LSW_AGENT_INFO("host link: {} (problem {}); to do: {}", device_text(seen.state.device),
                           seen.problem, list_text(needed));
        }
        last_needed_ = needed;
    }
    for (const HostLinkRepair repair : needed) {
        if (!backoff.due(repair, now)) {
            continue;
        }
        backoff.tried(repair, now);
        const bool done = apply(repair, seen);
        LSW_AGENT_INFO("host link: {} {} (attempt {})", HostLinkPlan::name(repair),
                       done ? "done" : "failed", backoff.attempts(repair));
    }
}

}  // namespace lsw::agent::system
