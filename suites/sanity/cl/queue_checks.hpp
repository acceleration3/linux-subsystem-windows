#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class QueueChecks {
public:
    [[nodiscard]] static Outcome multiple_queues_concurrent(Context& context);

    [[nodiscard]] static Outcome finish_flush_release(Context& context);
};
}  // namespace lsw::suites::sanity::cl
