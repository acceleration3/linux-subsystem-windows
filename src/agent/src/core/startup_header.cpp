#include "lsw/agent/core/startup_header.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/system/info.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

namespace lsw::agent::core {

namespace {

std::string windows_build() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    const auto fn = ntdll != nullptr
                        ? reinterpret_cast<RtlGetVersionFn>(
                              reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")))
                        : nullptr;
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (fn == nullptr || fn(&v) != 0) {
        return "unknown";
    }
    return std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + " build " +
           std::to_string(v.dwBuildNumber);
}

std::string text_of(const nlohmann::json& record, const char* key) {
    return record.is_object() && record.value(key, nlohmann::json{}).is_string()
               ? record[key].get<std::string>()
               : std::string{};
}

}

void StartupHeader::write(const std::string& role) {
    LSW_AGENT_INFO("lsw-agent {} ({}), pid {}", system::Info::build_stamp(), role,
                   ::GetCurrentProcessId());
    const auto release = system::Info::release_record();
    const std::string id = text_of(release, "release");
    const std::string version = text_of(release, "version");
    LSW_AGENT_INFO("release: {}{}", id.empty() ? "none recorded" : id,
                   version.empty() ? "" : " (LSW " + version + ")");
    LSW_AGENT_INFO("Windows {}", windows_build());
}

}
