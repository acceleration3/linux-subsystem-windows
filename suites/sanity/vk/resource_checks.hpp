#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class ResourceChecks {
public:
    [[nodiscard]] static Outcome all_usages(Context& context);

    [[nodiscard]] static Outcome formats_features_usage(Context& context);

    [[nodiscard]] static Outcome types_layers_mips(Context& context);

    [[nodiscard]] static Outcome sparse_caps(Context& context);
};
}  // namespace lsw::suites::sanity::vk
