#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class ShaderChecks {
public:
    [[nodiscard]] static Outcome long_running(Context& context);

    [[nodiscard]] static Outcome infinite(Context& context);
};
}  // namespace lsw::suites::sanity::err
