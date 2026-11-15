#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::enumeration {
class IdentityChecks {
public:
    [[nodiscard]] static Outcome dxgi_vs_kmt_luid(Context& context);

    [[nodiscard]] static Outcome vulkan_luid_matches(Context& context);

    [[nodiscard]] static Outcome opencl_luid_matches(Context& context);

    [[nodiscard]] static Outcome gl_renderer_matches(Context& context);

    [[nodiscard]] static Outcome hybrid_roles(Context& context);

    [[nodiscard]] static Outcome setupapi_matches(Context& context);

    [[nodiscard]] static Outcome names_nonempty_printable(Context& context);
};
}  // namespace lsw::suites::sanity::enumeration
