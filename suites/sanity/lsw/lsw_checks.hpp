#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::lsw {
class LswChecks {
public:
    [[nodiscard]] static Outcome launched_app_appears(Context& context);

    [[nodiscard]] static Outcome relaunching_app(Context& context);

    [[nodiscard]] static Outcome frames_equal_guest(Context& context);

    [[nodiscard]] static Outcome maximize_work_area(Context& context);

    [[nodiscard]] static Outcome snap_restore(Context& context);

    [[nodiscard]] static Outcome move_resize_guest_follows(Context& context);

    [[nodiscard]] static Outcome keyboard_to_right_window(Context& context);

    [[nodiscard]] static Outcome mouse_position_mapping(Context& context);

    [[nodiscard]] static Outcome focus_follows(Context& context);

    [[nodiscard]] static Outcome status_numbers_match_activity(Context& context);

    [[nodiscard]] static Outcome update_preserves_requirements(Context& context);

    [[nodiscard]] static Outcome window_close_ends_app(Context& context);

    [[nodiscard]] static Outcome maximized_not_at_drop_point(Context& context);

    [[nodiscard]] static Outcome first_launch_mirrored(Context& context);
};
}  // namespace lsw::suites::sanity::lsw
