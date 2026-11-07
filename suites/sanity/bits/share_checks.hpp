#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::bits {
class ShareChecks {
public:
    [[nodiscard]] static Outcome cross_bitness_nt_handle(Context& context);

    [[nodiscard]] static Outcome cross_bitness_fence(Context& context);

    [[nodiscard]] static Outcome cross_bitness_dxgi_keyed_mutex(Context& context);
};
}  // namespace lsw::suites::sanity::bits
