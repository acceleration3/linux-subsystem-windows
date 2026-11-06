#include "lsw/host/services/guest_gpu_status.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>

using lsw::host::services::GuestGpuStatus;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

}

int main() {
    const auto loaded = GuestGpuStatus::from_json(nlohmann::json::parse(
        R"({"adapters": [], "display_core_disabled": true, "display_core_loaded": true})"));
    check(loaded.valid && loaded.display_core_disabled, "the disabled flag is read");
    check(loaded.display_core_loaded.has_value() && *loaded.display_core_loaded,
          "a loaded DisplayCore is read");

    const auto clean = GuestGpuStatus::from_json(nlohmann::json::parse(
        R"({"display_core_disabled": true, "display_core_loaded": false})"));
    check(clean.display_core_loaded.has_value() && !*clean.display_core_loaded,
          "a DisplayCore that is not loaded is read");

    const auto unknown = GuestGpuStatus::from_json(nlohmann::json::parse(
        R"({"display_core_disabled": false, "display_core_loaded": null})"));
    check(!unknown.display_core_disabled && !unknown.display_core_loaded.has_value(),
          "null means the loaded state is unknown");

    const auto absent = GuestGpuStatus::from_json(nlohmann::json::parse(R"({"adapters": []})"));
    check(absent.valid && !absent.display_core_disabled && !absent.display_core_loaded.has_value(),
          "absent fields mean not disabled and unknown");

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
