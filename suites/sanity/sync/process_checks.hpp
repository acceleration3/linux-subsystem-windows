#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::sync {
class ProcessChecks {
public:
    [[nodiscard]] static Outcome many_processes(Context& context);

    [[nodiscard]] static Outcome shared_fence_ping_pong(Context& context);
};
}  // namespace lsw::suites::sanity::sync
