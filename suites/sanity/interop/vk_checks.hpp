#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class VkChecks {
public:
    [[nodiscard]] static Outcome import_d3d11_texture(Context& context);

    [[nodiscard]] static Outcome import_d3d12_resource(Context& context);

    [[nodiscard]] static Outcome export_to_d3d12(Context& context);

    [[nodiscard]] static Outcome export_to_gl(Context& context);

    [[nodiscard]] static Outcome opencl_external_memory_caps(Context& context);

    [[nodiscard]] static Outcome kmt_handles_caps(Context& context);

    [[nodiscard]] static Outcome cross_process_present_chain(Context& context);
};
}  // namespace lsw::suites::sanity::interop
