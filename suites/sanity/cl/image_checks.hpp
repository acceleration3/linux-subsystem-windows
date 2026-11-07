#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class ImageChecks {
public:
    [[nodiscard]] static Outcome formats_supported(Context& context);
};
}  // namespace lsw::suites::sanity::cl
