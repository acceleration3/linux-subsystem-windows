#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::enumeration {
class CapsChecks {
public:
    [[nodiscard]] static Outcome feature_level_vs_api_support(Context& context);
};
}  // namespace lsw::suites::sanity::enumeration
