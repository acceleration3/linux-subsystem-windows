#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class AlphaChecks {
public:
    [[nodiscard]] static Outcome alpha_modes(Context& context);
};
}  // namespace lsw::suites::sanity::present
