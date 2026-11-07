#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class ComputeChecks {
public:
    [[nodiscard]] static Outcome uav_basic(Context& context);

    [[nodiscard]] static Outcome atomics(Context& context);

    [[nodiscard]] static Outcome append_consume(Context& context);

    [[nodiscard]] static Outcome typed_uav_load(Context& context);

    [[nodiscard]] static Outcome barriers_sharing(Context& context);

    [[nodiscard]] static Outcome cs_on_downlevel(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
