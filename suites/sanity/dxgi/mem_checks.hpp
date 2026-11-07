#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::dxgi {
class MemChecks {
public:
    [[nodiscard]] static Outcome video_memory_info(Context& context);

    [[nodiscard]] static Outcome offer_reclaim(Context& context);

    [[nodiscard]] static Outcome trim_and_priority(Context& context);

    [[nodiscard]] static Outcome overlay_caps_consistent(Context& context);

    [[nodiscard]] static Outcome overlay_mpo_present(Context& context);

    [[nodiscard]] static Outcome debug_no_messages(Context& context);

    [[nodiscard]] static Outcome removal_events(Context& context);

    [[nodiscard]] static Outcome duplication_visible(Context& context);
};
}  // namespace lsw::suites::sanity::dxgi
