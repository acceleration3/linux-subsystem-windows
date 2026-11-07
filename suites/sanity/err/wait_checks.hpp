#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class WaitChecks {
public:
    [[nodiscard]] static Outcome never_signalled(Context& context);
};
}  // namespace lsw::suites::sanity::err
