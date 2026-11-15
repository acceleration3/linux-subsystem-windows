#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class ErrChecks {
public:
    [[nodiscard]] static Outcome bad_parameters(Context& context);

    [[nodiscard]] static Outcome device_removed(Context& context);

    [[nodiscard]] static Outcome stress_destroy(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
