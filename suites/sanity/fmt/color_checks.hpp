#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class ColorChecks {
public:
    [[nodiscard]] static Outcome srgb_conversion(Context& context);

    [[nodiscard]] static Outcome typeless_casts(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
