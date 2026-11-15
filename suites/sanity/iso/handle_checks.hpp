#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::iso {
class HandleChecks {
public:
    [[nodiscard]] static Outcome no_access_without_handle(Context& context);

    [[nodiscard]] static Outcome closed_handle_dead(Context& context);

    [[nodiscard]] static Outcome read_only_enforced(Context& context);

    [[nodiscard]] static Outcome service_session_separate(Context& context);
};
}  // namespace lsw::suites::sanity::iso
