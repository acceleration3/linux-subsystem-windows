#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::sync {
class CrossChecks {
public:
    [[nodiscard]] static Outcome d3d11_d3d12_fence(Context& context);

    [[nodiscard]] static Outcome vulkan_d3d12_semaphore(Context& context);

    [[nodiscard]] static Outcome gl_d3d11_interop(Context& context);
};
}  // namespace lsw::suites::sanity::sync
