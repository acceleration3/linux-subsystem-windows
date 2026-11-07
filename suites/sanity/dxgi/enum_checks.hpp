#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::dxgi {
class EnumChecks {
public:
    [[nodiscard]] static Outcome adapters_consistent(Context& context);

    [[nodiscard]] static Outcome factory_lifecycle(Context& context);

    [[nodiscard]] static Outcome adapter_desc_matches_apis(Context& context);

    [[nodiscard]] static Outcome output_modes(Context& context);

    [[nodiscard]] static Outcome output_desc_vs_win32(Context& context);
};
}  // namespace lsw::suites::sanity::dxgi
