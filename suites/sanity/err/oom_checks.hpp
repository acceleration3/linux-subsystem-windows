#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class OomChecks {
public:
    [[nodiscard]] static Outcome every_api(Context& context);
};
}  // namespace lsw::suites::sanity::err
