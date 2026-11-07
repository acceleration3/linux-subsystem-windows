#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class TiledChecks {
public:
    [[nodiscard]] static Outcome caps_driven(Context& context);

    [[nodiscard]] static Outcome d3d11_1(Context& context);

    [[nodiscard]] static Outcome d3d11_2_3_4(Context& context);

    [[nodiscard]] static Outcome on12(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
