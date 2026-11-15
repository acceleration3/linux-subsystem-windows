#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::capture {
class ThumbnailChecks {
public:
    [[nodiscard]] static Outcome each_api(Context& context);

    [[nodiscard]] static Outcome updates(Context& context);
};
}  // namespace lsw::suites::sanity::capture
