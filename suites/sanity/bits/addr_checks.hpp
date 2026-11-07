#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::bits {
class AddrChecks {
public:
    [[nodiscard]] static Outcome allocation_fails_cleanly(Context& context);

    [[nodiscard]] static Outcome large_address_aware_on(Context& context);

    [[nodiscard]] static Outcome large_address_aware_off(Context& context);

    [[nodiscard]] static Outcome icd_loading(Context& context);

    [[nodiscard]] static Outcome exit_cleanup(Context& context);
};
}  // namespace lsw::suites::sanity::bits
