#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class CapsChecks {
public:
    [[nodiscard]] static Outcome create_matrix(Context& context);

    [[nodiscard]] static Outcome caps_vs_behaviour(Context& context);

    [[nodiscard]] static Outcome check_device_format_consistent(Context& context);

    [[nodiscard]] static Outcome ex_interface(Context& context);

    [[nodiscard]] static Outcome adapter_identity(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
