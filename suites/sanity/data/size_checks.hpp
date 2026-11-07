#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::data {
class SizeChecks {
public:
    [[nodiscard]] static Outcome buffer_roundtrip_small(Context& context);

    [[nodiscard]] static Outcome buffer_roundtrip_boundaries(Context& context);

    [[nodiscard]] static Outcome buffer_roundtrip_hundreds_mib(Context& context);

    [[nodiscard]] static Outcome texture_upload_sizes(Context& context);

    [[nodiscard]] static Outcome texture_full_screen_upload(Context& context);

    [[nodiscard]] static Outcome readback_every_size(Context& context);
};
}  // namespace lsw::suites::sanity::data
