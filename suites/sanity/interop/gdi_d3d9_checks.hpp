#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GdiD3d9Checks {
public:
    [[nodiscard]] static Outcome surface_getdc(Context& context);

    [[nodiscard]] static Outcome flip_getdc(Context& context);

    [[nodiscard]] static Outcome after_device_lost(Context& context);
};
}  // namespace lsw::suites::sanity::interop
