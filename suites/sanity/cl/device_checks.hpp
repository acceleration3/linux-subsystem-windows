#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class DeviceChecks {
public:
    [[nodiscard]] static Outcome query_all_params(Context& context);

    [[nodiscard]] static Outcome extensions_match_entry_points(Context& context);

    [[nodiscard]] static Outcome limits_honoured(Context& context);

    [[nodiscard]] static Outcome both_adapters(Context& context);

    [[nodiscard]] static Outcome create_sub_devices_error(Context& context);
};
}  // namespace lsw::suites::sanity::cl
