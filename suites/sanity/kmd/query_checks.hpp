#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::kmd {
class QueryChecks {
public:
    [[nodiscard]] static Outcome adapter_info_stable(Context& context);

    [[nodiscard]] static Outcome size_semantics(Context& context);

    [[nodiscard]] static Outcome unsupported_type_status(Context& context);

    [[nodiscard]] static Outcome segment_info_consistent(Context& context);

    [[nodiscard]] static Outcome driver_caps_self_consistent(Context& context);

    [[nodiscard]] static Outcome open_adapter_from_hdc_and_name(Context& context);

    [[nodiscard]] static Outcome close_adapter_twice(Context& context);

    [[nodiscard]] static Outcome escape_malformed_rejected(Context& context);

    [[nodiscard]] static Outcome escape_unknown_command_status(Context& context);
};
}  // namespace lsw::suites::sanity::kmd
