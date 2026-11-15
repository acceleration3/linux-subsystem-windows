#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::data {
class AlignChecks {
public:
    [[nodiscard]] static Outcome offset_every_byte(Context& context);

    [[nodiscard]] static Outcome unaligned_copy_regions(Context& context);

    [[nodiscard]] static Outcome mapped_pointer_alignment(Context& context);

    [[nodiscard]] static Outcome row_pitch_variants(Context& context);

    [[nodiscard]] static Outcome sub_region_updates(Context& context);
};
}  // namespace lsw::suites::sanity::data
