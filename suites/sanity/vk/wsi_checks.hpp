#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class WsiChecks {
public:
    [[nodiscard]] static Outcome surface_caps_consistent(Context& context);

    [[nodiscard]] static Outcome fifo_every_image_in_order(Context& context);

    [[nodiscard]] static Outcome mailbox_immediate_caps(Context& context);

    [[nodiscard]] static Outcome out_of_date_suboptimal(Context& context);

    [[nodiscard]] static Outcome acquire_timeouts(Context& context);

    [[nodiscard]] static Outcome minimized_and_destroyed(Context& context);
};
}  // namespace lsw::suites::sanity::vk
