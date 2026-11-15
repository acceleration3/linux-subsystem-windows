#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::env {
class InstallChecks {
public:
    [[nodiscard]] static Outcome adapter_started(Context& context);

    [[nodiscard]] static Outcome driver_version_consistent(Context& context);

    [[nodiscard]] static Outcome umd_files_present_both_bitnesses(Context& context);

    [[nodiscard]] static Outcome signed_and_loadable(Context& context);

    [[nodiscard]] static Outcome no_duplicate_adapters(Context& context);

    [[nodiscard]] static Outcome requirements_hold(Context& context);
};
}  // namespace lsw::suites::sanity::env
