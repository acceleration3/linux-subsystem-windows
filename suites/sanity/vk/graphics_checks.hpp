#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class GraphicsChecks {
public:
    [[nodiscard]] static Outcome vs_twin(Context& context);

    [[nodiscard]] static Outcome pipeline_features(Context& context);
};
}  // namespace lsw::suites::sanity::vk
