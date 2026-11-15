#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::env {
class DisplayChecks {
public:
    [[nodiscard]] static Outcome output_present(Context& context);

    [[nodiscard]] static Outcome output_active_mode(Context& context);

    [[nodiscard]] static Outcome desktop_rect_consistent(Context& context);

    [[nodiscard]] static Outcome dpi_scaling_consistent(Context& context);
};
}  // namespace lsw::suites::sanity::env
