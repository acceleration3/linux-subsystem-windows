#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class D3dChecks {
public:
    [[nodiscard]] static Outcome d3d9ex_shared_to_d3d11(Context& context);

    [[nodiscard]] static Outcome d3d11_nt_handle_d3d12(Context& context);

    [[nodiscard]] static Outcome shared_fence_ordering(Context& context);

    [[nodiscard]] static Outcome keyed_mutex(Context& context);

    [[nodiscard]] static Outcome d3d11on12(Context& context);
};
}  // namespace lsw::suites::sanity::interop
