#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::iso {
class FreshChecks {
public:
    [[nodiscard]] static Outcome fresh_alloc_zero_or_unspecified(Context& context);

    [[nodiscard]] static Outcome fresh_vram_pattern(Context& context);

    [[nodiscard]] static Outcome fresh_host_visible(Context& context);
};
}  // namespace lsw::suites::sanity::iso
