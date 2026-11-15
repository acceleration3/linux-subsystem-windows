#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GdiDxgiChecks {
public:
    [[nodiscard]] static Outcome getdc_roundtrip(Context& context);

    [[nodiscard]] static Outcome getdc_discard_flag(Context& context);

    [[nodiscard]] static Outcome getdc_lifetime(Context& context);
};
}  // namespace lsw::suites::sanity::interop
