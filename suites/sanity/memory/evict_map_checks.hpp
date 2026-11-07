#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::memory {
class EvictMapChecks {
public:
    [[nodiscard]] static Outcome contents_survive_pressure(Context& context);

    [[nodiscard]] static Outcome make_resident_semantics(Context& context);

    [[nodiscard]] static Outcome pointer_valid_until_unmap(Context& context);

    [[nodiscard]] static Outcome unmap_flushes(Context& context);

    [[nodiscard]] static Outcome exit_with_live_mappings(Context& context);

    [[nodiscard]] static Outcome large_map_many(Context& context);
};
}  // namespace lsw::suites::sanity::memory
