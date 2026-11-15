#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::sync {
class WaitChecks {
public:
    [[nodiscard]] static Outcome timeout_honoured(Context& context);

    [[nodiscard]] static Outcome zero_timeout_poll(Context& context);

    [[nodiscard]] static Outcome signal_before_wait(Context& context);
};
}  // namespace lsw::suites::sanity::sync
