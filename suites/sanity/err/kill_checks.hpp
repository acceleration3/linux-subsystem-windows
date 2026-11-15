#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class KillChecks {
public:
    [[nodiscard]] static Outcome mid_submit(Context& context);
};
}  // namespace lsw::suites::sanity::err
