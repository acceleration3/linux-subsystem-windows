#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::shader {
class DxbcChecks {
public:
    [[nodiscard]] static Outcome sm2_sm3(Context& context);

    [[nodiscard]] static Outcome sm4_sm5(Context& context);

    [[nodiscard]] static Outcome invalid_diagnostics(Context& context);
};
}  // namespace lsw::suites::sanity::shader
