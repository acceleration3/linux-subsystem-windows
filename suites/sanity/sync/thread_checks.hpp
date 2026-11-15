#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::sync {
class ThreadChecks {
public:
    [[nodiscard]] static Outcome many_threads_one_device(Context& context);

    [[nodiscard]] static Outcome multi_queue_vulkan(Context& context);

    [[nodiscard]] static Outcome d3d12_multi_queue(Context& context);

    [[nodiscard]] static Outcome immediate_context_serialised(Context& context);
};
}  // namespace lsw::suites::sanity::sync
