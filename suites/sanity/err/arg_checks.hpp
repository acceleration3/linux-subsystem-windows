#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class ArgChecks {
public:
    [[nodiscard]] static Outcome d3d11_invalid_params(Context& context);

    [[nodiscard]] static Outcome d3d12_invalid_params(Context& context);

    [[nodiscard]] static Outcome d3d9_invalid_params(Context& context);

    [[nodiscard]] static Outcome vulkan_invalid_handles_null(Context& context);

    [[nodiscard]] static Outcome opencl_error_codes(Context& context);

    [[nodiscard]] static Outcome gl_error_flags(Context& context);

    [[nodiscard]] static Outcome dxgi_invalid_swapchain(Context& context);
};
}  // namespace lsw::suites::sanity::err
