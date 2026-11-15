#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class QueryChecks {
public:
    [[nodiscard]] static Outcome timestamps(Context& context);

    [[nodiscard]] static Outcome buffers_reset_reuse(Context& context);
};
}  // namespace lsw::suites::sanity::vk
