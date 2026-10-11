#include "lsw/agent/log.hpp"
#include "lsw/agent/rpc/auto_register.hpp"
#include "lsw/agent/rpc/handler.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/token_privileges.hpp"
#include "lsw/linkproto/commands.hpp"

#include <windows.h>

#include <cstdint>
#include <cwchar>
#include <optional>
#include <stdexcept>
#include <string>

namespace lsw::agent::rpc::handlers {

namespace {

// Off by less than this, the clock is left alone: the call itself takes
// a moment, and Windows' own time service keeps it closer than this.
constexpr std::int64_t kTolerance_ms = 2000;

// Milliseconds between 1601 (FILETIME's epoch) and 1970.
constexpr std::int64_t kEpochGap_ms = 11644473600000LL;

std::int64_t now_ms() {
    FILETIME now{};
    ::GetSystemTimeAsFileTime(&now);
    const auto ticks = (static_cast<std::int64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return ticks / 10000 - kEpochGap_ms;
}

std::wstring current_zone() {
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    if (::GetDynamicTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID) {
        return {};
    }
    return zone.TimeZoneKeyName;
}

// The zone Windows knows by ``name`` (its registry key: "GMT Standard
// Time"), or nullopt.
std::optional<DYNAMIC_TIME_ZONE_INFORMATION> find_zone(const std::wstring& name) {
    for (DWORD index = 0;; ++index) {
        DYNAMIC_TIME_ZONE_INFORMATION zone{};
        if (::EnumDynamicTimeZoneInformation(index, &zone) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        if (::_wcsicmp(zone.TimeZoneKeyName, name.c_str()) == 0) {
            return zone;
        }
    }
}

void set_zone(const std::wstring& name) {
    const auto zone = find_zone(name);
    if (!zone) {
        throw std::invalid_argument("Windows has no time zone named " +
                                    util::TextEncoding::to_utf8(name));
    }
    win::TokenPrivileges::enable(SE_TIME_ZONE_NAME);
    if (::SetDynamicTimeZoneInformation(&*zone) == FALSE) {
        throw std::runtime_error("SetDynamicTimeZoneInformation failed: " +
                                 win::ErrorText::last_error());
    }
}

void set_time(std::int64_t utc_ms) {
    const auto ticks = static_cast<std::uint64_t>(utc_ms + kEpochGap_ms) * 10000ULL;
    FILETIME when{};
    when.dwLowDateTime = static_cast<DWORD>(ticks & 0xffffffffULL);
    when.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    SYSTEMTIME system{};
    if (::FileTimeToSystemTime(&when, &system) == FALSE) {
        throw std::invalid_argument("utc_ms is not a time Windows can hold");
    }
    win::TokenPrivileges::enable(SE_SYSTEMTIME_NAME);
    if (::SetSystemTime(&system) == FALSE) {
        throw std::runtime_error("SetSystemTime failed: " + win::ErrorText::last_error());
    }
}

}  // namespace

// {"time_zone": string, "utc_ms": int} -> {"time_zone_was": string,
// "time_zone": string, "clock_off_ms": int, "clock_set": bool}
//
// Windows' time zone becomes ``time_zone``, the host's, and its clock the
// host's ``utc_ms`` when it is more than two seconds off. The VM's RTC
// runs on the host's local time and Windows reads it in its own zone, so
// in another zone the clock is hours off from every boot until the time
// service corrects it; in the host's it is right from the start. The
// zone is set first, which leaves the UTC time as it was; the clock
// after, against the host's.
struct SetClockHandler : RpcHandler {
    nlohmann::json handle(const nlohmann::json& params, RpcContext& /*ctx*/) override {
        const std::string zone = params.value("time_zone", std::string{});
        if (zone.empty() || !params.contains("utc_ms") || !params["utc_ms"].is_number_integer()) {
            throw std::invalid_argument("set_clock takes time_zone and utc_ms");
        }
        const std::wstring wanted = util::TextEncoding::to_wide(zone);
        const std::wstring was = current_zone();
        if (::_wcsicmp(was.c_str(), wanted.c_str()) != 0) {
            set_zone(wanted);
            LSW_AGENT_INFO("set_clock: time zone {} -> {}", util::TextEncoding::to_utf8(was), zone);
        }
        const auto host_ms = params["utc_ms"].get<std::int64_t>();
        const std::int64_t off = now_ms() - host_ms;
        const bool set = off > kTolerance_ms || off < -kTolerance_ms;
        if (set) {
            set_time(host_ms);
            LSW_AGENT_INFO("set_clock: the clock was {} ms off the host's; set to it", off);
        }
        return {
            {"time_zone_was", util::TextEncoding::to_utf8(was)},
            {"time_zone", util::TextEncoding::to_utf8(current_zone())},
            {"clock_off_ms", off},
            {"clock_set", set},
        };
    }
};

}  // namespace lsw::agent::rpc::handlers

LSW_REGISTER_RPC(lsw::agent::rpc::handlers::SetClockHandler, ::lsw::linkproto::cmd::kSetClock)
