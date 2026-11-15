#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class InstanceChecks {
public:
    [[nodiscard]] static Outcome version_layers_extensions(Context& context);

    [[nodiscard]] static Outcome props_features_consistent(Context& context);

    [[nodiscard]] static Outcome limits_minimums_honoured(Context& context);

    [[nodiscard]] static Outcome queue_families(Context& context);

    [[nodiscard]] static Outcome extensions_match_functions(Context& context);
};
}  // namespace lsw::suites::sanity::vk
