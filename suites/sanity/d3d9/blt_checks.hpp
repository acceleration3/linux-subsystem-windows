#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class BltChecks {
public:
    [[nodiscard]] static Outcome stretchrect(Context& context);

    [[nodiscard]] static Outcome updatesurface_texture(Context& context);

    [[nodiscard]] static Outcome colorfill_and_readback(Context& context);

    [[nodiscard]] static Outcome frontbuffer(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
