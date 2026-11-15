#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GlD3dChecks {
public:
    [[nodiscard]] static Outcome d3d11_objects(Context& context);

    [[nodiscard]] static Outcome d3d9ex_objects(Context& context);

    [[nodiscard]] static Outcome lock_ordering(Context& context);

    [[nodiscard]] static Outcome formats_and_targets(Context& context);

    [[nodiscard]] static Outcome present_via_d3d(Context& context);

    [[nodiscard]] static Outcome caps_unsupported(Context& context);
};
}  // namespace lsw::suites::sanity::interop
