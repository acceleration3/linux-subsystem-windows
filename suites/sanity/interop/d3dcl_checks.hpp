#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class D3dClChecks {
public:
    [[nodiscard]] static Outcome d3d11_sharing(Context& context);

    [[nodiscard]] static Outcome d3d10_sharing(Context& context);

    [[nodiscard]] static Outcome dx9_media_sharing(Context& context);

    [[nodiscard]] static Outcome acquire_order_caps(Context& context);
};
}  // namespace lsw::suites::sanity::interop
