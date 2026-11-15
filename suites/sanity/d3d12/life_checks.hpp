#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class LifeChecks {
public:
    [[nodiscard]] static Outcome removal_reasons(Context& context);

    [[nodiscard]] static Outcome lifetime_exit(Context& context);

    [[nodiscard]] static Outcome concurrent_queues(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
