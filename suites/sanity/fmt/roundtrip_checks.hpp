#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class RoundtripChecks {
public:
    [[nodiscard]] static Outcome roundtrip_uncompressed(Context& context);

    [[nodiscard]] static Outcome packed_formats(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
