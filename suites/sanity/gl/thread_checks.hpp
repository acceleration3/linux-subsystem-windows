#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class ThreadChecks {
public:
    [[nodiscard]] static Outcome share_across_threads(Context& context);

    [[nodiscard]] static Outcome make_current_rules(Context& context);

    [[nodiscard]] static Outcome concurrent_contexts(Context& context);

    [[nodiscard]] static Outcome producer_consumer(Context& context);
};
}  // namespace lsw::suites::sanity::gl
