#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class DisplayChecks {
public:
    [[nodiscard]] static Outcome display_formats(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
