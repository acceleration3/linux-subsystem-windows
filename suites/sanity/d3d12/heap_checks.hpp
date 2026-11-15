#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class HeapChecks {
public:
    [[nodiscard]] static Outcome default_upload_readback(Context& context);

    [[nodiscard]] static Outcome custom_properties(Context& context);

    [[nodiscard]] static Outcome placed_alignment(Context& context);

    [[nodiscard]] static Outcome aliasing(Context& context);

    [[nodiscard]] static Outcome reserved_tiles(Context& context);

    [[nodiscard]] static Outcome committed_flags(Context& context);

    [[nodiscard]] static Outcome zeroed_new(Context& context);

    [[nodiscard]] static Outcome exhaustion(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
