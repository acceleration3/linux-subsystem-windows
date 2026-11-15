#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class FootprintChecks {
public:
    [[nodiscard]] static Outcome mip_and_array_footprints(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
