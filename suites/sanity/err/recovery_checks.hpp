#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

#include <string>

namespace lsw::suites::sanity::err {
class RecoveryChecks {
public:
    [[nodiscard]] static Outcome child_role(Context& context, const std::string& name);

    [[nodiscard]] static Outcome device_removed_reason(Context& context);

    [[nodiscard]] static Outcome device_lost_vulkan(Context& context);

    [[nodiscard]] static Outcome tdr_recovers(Context& context);

    [[nodiscard]] static Outcome process_crash_cleanup(Context& context);

    [[nodiscard]] static Outcome oom_clean(Context& context);

    [[nodiscard]] static Outcome kill_during_map(Context& context);
};
}  // namespace lsw::suites::sanity::err
