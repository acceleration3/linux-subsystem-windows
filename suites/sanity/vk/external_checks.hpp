#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class ExternalChecks {
public:
    [[nodiscard]] static Outcome memory_caps_consistent(Context& context);

    [[nodiscard]] static Outcome semaphore_fence_caps(Context& context);
};
}  // namespace lsw::suites::sanity::vk
