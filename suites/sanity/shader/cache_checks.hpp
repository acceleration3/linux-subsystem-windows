#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::shader {
class CacheChecks {
public:
    [[nodiscard]] static Outcome reuse_correct(Context& context);
};
}  // namespace lsw::suites::sanity::shader
