#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class PlatformChecks {
public:
    [[nodiscard]] static Outcome query_consistent(Context& context);
};
}  // namespace lsw::suites::sanity::cl
