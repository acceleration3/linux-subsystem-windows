#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::env {
class IcdChecks {
public:
    [[nodiscard]] static Outcome opencl_registry_x64(Context& context);

    [[nodiscard]] static Outcome opencl_registry_x86(Context& context);

    [[nodiscard]] static Outcome vulkan_manifest_both(Context& context);

    [[nodiscard]] static Outcome opengl_icd_both(Context& context);

    [[nodiscard]] static Outcome loader_independent_of_cwd(Context& context);

private:
    [[nodiscard]] static Outcome opencl_registry(Context& context, int bitness);
};
}  // namespace lsw::suites::sanity::env
