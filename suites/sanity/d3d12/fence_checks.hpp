#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class FenceChecks {
public:
    [[nodiscard]] static Outcome cross_queue(Context& context);

    [[nodiscard]] static Outcome cpu_semantics(Context& context);

    [[nodiscard]] static Outcome shared(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
