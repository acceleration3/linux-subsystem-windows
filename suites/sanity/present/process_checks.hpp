#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::present {
class ProcessChecks {
public:
    [[nodiscard]] static Outcome many_processes(Context& context);
};
}  // namespace lsw::suites::sanity::present
