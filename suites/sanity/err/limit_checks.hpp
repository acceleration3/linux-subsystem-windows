#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class LimitChecks {
public:
    [[nodiscard]] static Outcome handle_count(Context& context);

    [[nodiscard]] static Outcome max_texture_dims(Context& context);

    [[nodiscard]] static Outcome max_threads_workgroup(Context& context);

    [[nodiscard]] static Outcome many_contexts_per_process(Context& context);
};
}  // namespace lsw::suites::sanity::err
