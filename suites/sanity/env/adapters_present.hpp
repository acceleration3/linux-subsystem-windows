#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::env {
class AdaptersPresent {
public:
    [[nodiscard]] static Outcome run(Context& context);
};
}  // namespace lsw::suites::sanity::env
