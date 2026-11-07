#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class ErrorChecks {
public:
    [[nodiscard]] static Outcome codes_documented(Context& context);
};
}  // namespace lsw::suites::sanity::cl
