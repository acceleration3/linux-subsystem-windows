#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class QueueChecks {
public:
    [[nodiscard]] static Outcome types(Context& context);

    [[nodiscard]] static Outcome many_lists(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
