#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::shader {
class GlslChecks {
public:
    [[nodiscard]] static Outcome versions(Context& context);

    [[nodiscard]] static Outcome invalid_diagnostics(Context& context);
};
}  // namespace lsw::suites::sanity::shader
