#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class MapChecks {
public:
    [[nodiscard]] static Outcome read_last_write(Context& context);

    [[nodiscard]] static Outcome discard_old_draws(Context& context);

    [[nodiscard]] static Outcome nooverwrite_inflight(Context& context);

    [[nodiscard]] static Outcome do_not_wait(Context& context);

    [[nodiscard]] static Outcome pitches(Context& context);

    [[nodiscard]] static Outcome errors(Context& context);

    [[nodiscard]] static Outcome persistent_across_frames(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
