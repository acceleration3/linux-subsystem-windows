#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class ShareChecks {
public:
    [[nodiscard]] static Outcome legacy_handle(Context& context);

    [[nodiscard]] static Outcome nt_handle(Context& context);

    [[nodiscard]] static Outcome keyed_mutex(Context& context);

    [[nodiscard]] static Outcome fence(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
