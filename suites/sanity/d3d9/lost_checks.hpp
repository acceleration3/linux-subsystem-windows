#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class LostChecks {
public:
    [[nodiscard]] static Outcome reset_semantics(Context& context);

    [[nodiscard]] static Outcome ex_never_lost(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
