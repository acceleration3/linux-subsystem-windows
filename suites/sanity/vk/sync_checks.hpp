#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class SyncChecks {
public:
    [[nodiscard]] static Outcome fences(Context& context);

    [[nodiscard]] static Outcome binary_semaphores(Context& context);

    [[nodiscard]] static Outcome timeline_semaphores(Context& context);

    [[nodiscard]] static Outcome events_barriers(Context& context);

    [[nodiscard]] static Outcome memory_model(Context& context);

    [[nodiscard]] static Outcome submit2_and_wait_idle(Context& context);
};
}  // namespace lsw::suites::sanity::vk
