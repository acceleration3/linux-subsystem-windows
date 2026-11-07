#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class ContentChecks {
public:
    [[nodiscard]] static Outcome exact_pixels(Context& context);

    [[nodiscard]] static Outcome sizes_and_formats(Context& context);

    [[nodiscard]] static Outcome scaling_modes(Context& context);
};
}  // namespace lsw::suites::sanity::present
