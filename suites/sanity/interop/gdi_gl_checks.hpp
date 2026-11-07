#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GdiGlChecks {
public:
    [[nodiscard]] static Outcome window_owndc(Context& context);

    [[nodiscard]] static Outcome mixed_swap(Context& context);
};
}  // namespace lsw::suites::sanity::interop
