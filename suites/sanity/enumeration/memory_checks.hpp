#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::enumeration {
class MemoryChecks {
public:
    [[nodiscard]] static Outcome dedicated_vs_shared_consistent(Context& context);

    [[nodiscard]] static Outcome opencl_global_size(Context& context);
};
}  // namespace lsw::suites::sanity::enumeration
