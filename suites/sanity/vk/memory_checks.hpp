#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::vk {
class MemoryChecks {
public:
    [[nodiscard]] static Outcome every_type_allocates(Context& context);

    [[nodiscard]] static Outcome host_visible_map(Context& context);

    [[nodiscard]] static Outcome host_coherent_no_flush(Context& context);

    [[nodiscard]] static Outcome noncoherent_flush_invalidate(Context& context);

    [[nodiscard]] static Outcome device_local_and_budget(Context& context);

    [[nodiscard]] static Outcome allocation_limits(Context& context);

    [[nodiscard]] static Outcome dedicated_and_aliasing(Context& context);
};
}  // namespace lsw::suites::sanity::vk
