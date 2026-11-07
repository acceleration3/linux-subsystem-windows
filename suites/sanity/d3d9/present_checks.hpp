#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class PresentChecks {
public:
    [[nodiscard]] static Outcome swap_effects(Context& context);

    [[nodiscard]] static Outcome intervals(Context& context);

    [[nodiscard]] static Outcome presentex_flags(Context& context);

    [[nodiscard]] static Outcome src_dst_rects(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
