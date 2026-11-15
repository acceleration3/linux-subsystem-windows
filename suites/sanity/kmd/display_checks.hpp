#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::kmd {
class DisplayChecks {
public:
    [[nodiscard]] static Outcome vblank_wait_cadence(Context& context);

    [[nodiscard]] static Outcome scanline_monotonic(Context& context);

    [[nodiscard]] static Outcome vblank_vs_dwm_timing(Context& context);

    [[nodiscard]] static Outcome source_visible_while_composed(Context& context);

    [[nodiscard]] static Outcome mode_set_restore(Context& context);

    [[nodiscard]] static Outcome mode_list_all_settable(Context& context);

    [[nodiscard]] static Outcome cursor_shapes(Context& context);

    [[nodiscard]] static Outcome cursor_position_roundtrip(Context& context);

    [[nodiscard]] static Outcome gamma_ramp_roundtrip(Context& context);

    [[nodiscard]] static Outcome output_duplication_frame(Context& context);
};
}  // namespace lsw::suites::sanity::kmd
