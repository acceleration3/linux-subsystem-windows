#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class D3dProcessChecks {
public:
    [[nodiscard]] static Outcome cross_adapter_render_only(Context& context);

    [[nodiscard]] static Outcome cross_process_sharing(Context& context);

    [[nodiscard]] static Outcome cross_bitness_sharing(Context& context);

    [[nodiscard]] static Outcome handle_lifetime(Context& context);

    [[nodiscard]] static Outcome invalid_open_errors(Context& context);
};
}  // namespace lsw::suites::sanity::interop
