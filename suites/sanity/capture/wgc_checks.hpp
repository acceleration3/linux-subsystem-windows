#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::capture {
class WgcChecks {
public:
    [[nodiscard]] static Outcome each_api_equals_presented(Context& context);

    [[nodiscard]] static Outcome frames_in_order(Context& context);

    [[nodiscard]] static Outcome resize_and_close(Context& context);
};
}  // namespace lsw::suites::sanity::capture
