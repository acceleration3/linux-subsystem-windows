#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class TransferChecks {
public:
    [[nodiscard]] static Outcome update_boxes_pitches(Context& context);

    [[nodiscard]] static Outcome update_bc_and_formats(Context& context);

    [[nodiscard]] static Outcome update_copy_flags(Context& context);

    [[nodiscard]] static Outcome update_deferred_ctx(Context& context);

    [[nodiscard]] static Outcome copy_variants(Context& context);

    [[nodiscard]] static Outcome copy_typeless_cross_format(Context& context);

    [[nodiscard]] static Outcome copy_structure_copy(Context& context);

    [[nodiscard]] static Outcome copy_resolve(Context& context);

    [[nodiscard]] static Outcome copy_generatemips(Context& context);

    [[nodiscard]] static Outcome copy_clearview_discard(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
