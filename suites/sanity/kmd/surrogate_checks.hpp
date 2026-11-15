#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::kmd {
class SurrogateChecks {
public:
    [[nodiscard]] static Outcome lock_returns_written_data(Context& context);

    [[nodiscard]] static Outcome lock_twice_status(Context& context);

    [[nodiscard]] static Outcome lock_pointer_stable_while_locked(Context& context);

    [[nodiscard]] static Outcome discard_and_nooverwrite(Context& context);

    [[nodiscard]] static Outcome destroy_while_locked(Context& context);

    [[nodiscard]] static Outcome nt_handle_share(Context& context);

    [[nodiscard]] static Outcome nt_handle_wrong_adapter(Context& context);

    [[nodiscard]] static Outcome share_requires_handle(Context& context);

    [[nodiscard]] static Outcome monitored_fence_monotonic(Context& context);

    [[nodiscard]] static Outcome wait_releases_on_signal(Context& context);

    [[nodiscard]] static Outcome wait_timeout(Context& context);

    [[nodiscard]] static Outcome shared_fence_across_processes(Context& context);

    [[nodiscard]] static Outcome cpu_signal_visible_to_gpu(Context& context);

    [[nodiscard]] static Outcome tdr_restores_service(Context& context);

    [[nodiscard]] static Outcome tdr_other_adapter_unaffected(Context& context);

    [[nodiscard]] static Outcome power_transition_contents(Context& context);

    [[nodiscard]] static Outcome query_status_after_reset(Context& context);
};
}  // namespace lsw::suites::sanity::kmd
