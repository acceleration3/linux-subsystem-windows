#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class RobustChecks {
public:
    [[nodiscard]] static Outcome independent_devices(Context& context);

    [[nodiscard]] static Outcome reference_workload(Context& context);
};
}  // namespace lsw::suites::sanity::vk
