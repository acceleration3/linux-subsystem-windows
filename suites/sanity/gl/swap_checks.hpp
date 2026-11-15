#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class SwapChecks {
public:
    [[nodiscard]] static Outcome interval_semantics(Context& context);

    [[nodiscard]] static Outcome swapbuffers_content(Context& context);

    [[nodiscard]] static Outcome resize_and_dc_lifetime(Context& context);
};
}  // namespace lsw::suites::sanity::gl
