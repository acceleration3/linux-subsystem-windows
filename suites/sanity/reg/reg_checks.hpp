#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::reg {
class RegChecks {
public:
    [[nodiscard]] static Outcome cl_buffer_alive_while_in_use(Context& context);

    [[nodiscard]] static Outcome d3d12_caps_enum_defined(Context& context);

    [[nodiscard]] static Outcome stats_transfer_counts_plausible(Context& context);

    [[nodiscard]] static Outcome upload_large_not_slower_than_small(Context& context);

    [[nodiscard]] static Outcome dwm_output_not_toggled(Context& context);

    [[nodiscard]] static Outcome gl_core_context_works(Context& context);

    [[nodiscard]] static Outcome objects_many_objects_work(Context& context);

    [[nodiscard]] static Outcome gl_legacy_context_version_string(Context& context);
};
}  // namespace lsw::suites::sanity::reg
