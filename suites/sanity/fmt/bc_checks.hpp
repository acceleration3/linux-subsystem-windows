#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class BcChecks {
public:
    [[nodiscard]] static Outcome bc_blocks(Context& context);

    [[nodiscard]] static Outcome bc_dimensions(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
