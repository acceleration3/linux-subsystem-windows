#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::life {
class LifeChecks {
public:
    [[nodiscard]] static Outcome dwm_restart_apps_survive(Context& context);

    [[nodiscard]] static Outcome dwm_restart_desktop_recovers(Context& context);

    [[nodiscard]] static Outcome mode_change_apps_survive(Context& context);

    [[nodiscard]] static Outcome mode_refresh_rate_change(Context& context);

    [[nodiscard]] static Outcome sleep_resume_contents(Context& context);

    [[nodiscard]] static Outcome agent_restart_apps_unaffected(Context& context);

    [[nodiscard]] static Outcome agent_restart_new_apps_work(Context& context);

    [[nodiscard]] static Outcome live_driver_update(Context& context);

    [[nodiscard]] static Outcome session_logoff_logon(Context& context);

    [[nodiscard]] static Outcome display_output_off_on(Context& context);
};
}  // namespace lsw::suites::sanity::life
