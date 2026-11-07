#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::iso {
class FaultChecks {
public:
    [[nodiscard]] static Outcome crash_leaves_others(Context& context);

    [[nodiscard]] static Outcome device_loss_leaves_others(Context& context);

    [[nodiscard]] static Outcome oom_leaves_others(Context& context);

    [[nodiscard]] static Outcome hang_leaves_others(Context& context);

    [[nodiscard]] static Outcome invalid_commands_contained(Context& context);

    [[nodiscard]] static Outcome out_of_bounds_contained(Context& context);
};
}  // namespace lsw::suites::sanity::iso
