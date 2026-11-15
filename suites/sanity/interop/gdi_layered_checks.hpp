#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GdiLayeredChecks {
public:
    [[nodiscard]] static Outcome per_pixel_alpha(Context& context);

    [[nodiscard]] static Outcome over_3d_update(Context& context);

    [[nodiscard]] static Outcome text_over_flip_window(Context& context);

    [[nodiscard]] static Outcome mixed_windows(Context& context);
};
}  // namespace lsw::suites::sanity::interop
