#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::diag {
class DiagChecks {
public:
    [[nodiscard]] static Outcome transfer_splits(Context& context);

    [[nodiscard]] static Outcome transfer_arena_size(Context& context);

    [[nodiscard]] static Outcome transfer_refusals(Context& context);

    [[nodiscard]] static Outcome transfer_slice_waits(Context& context);

    [[nodiscard]] static Outcome memory_lent_pages(Context& context);

    [[nodiscard]] static Outcome memory_host_window_use(Context& context);

    [[nodiscard]] static Outcome stats_channel_counters(Context& context);

    [[nodiscard]] static Outcome registry_object_table(Context& context);

    [[nodiscard]] static Outcome sched_queue_depth(Context& context);

    [[nodiscard]] static Outcome latency_roundtrip_percentiles(Context& context);
};
}  // namespace lsw::suites::sanity::diag
