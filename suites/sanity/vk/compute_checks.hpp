#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class ComputeChecks {
public:
    [[nodiscard]] static Outcome vs_cpu(Context& context);

    [[nodiscard]] static Outcome pools_templates_samplers(Context& context);
};
}  // namespace lsw::suites::sanity::vk
