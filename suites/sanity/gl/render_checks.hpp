#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::gl {
class RenderChecks {
public:
    [[nodiscard]] static Outcome completeness_matrix(Context& context);

    [[nodiscard]] static Outcome blit(Context& context);

    [[nodiscard]] static Outcome core_vs_compat_equal(Context& context);

    [[nodiscard]] static Outcome instanced_indirect(Context& context);

    [[nodiscard]] static Outcome large_batches(Context& context);
};
}  // namespace lsw::suites::sanity::gl
