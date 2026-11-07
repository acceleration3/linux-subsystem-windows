#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class WindowChecks {
public:
    [[nodiscard]] static Outcome resize_during_presentation(Context& context);

    [[nodiscard]] static Outcome occlusion(Context& context);

    [[nodiscard]] static Outcome minimize_restore(Context& context);

    [[nodiscard]] static Outcome fullscreen_exclusive(Context& context);

    [[nodiscard]] static Outcome borderless_fullscreen_flip(Context& context);

    [[nodiscard]] static Outcome many_windows(Context& context);

    [[nodiscard]] static Outcome multi_monitor(Context& context);

    [[nodiscard]] static Outcome destroy_while_presenting(Context& context);
};
}  // namespace lsw::suites::sanity::present
