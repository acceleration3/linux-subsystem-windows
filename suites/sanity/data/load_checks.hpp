#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::data {
class LoadChecks {
public:
    [[nodiscard]] static Outcome parallel_processes(Context& context);

    [[nodiscard]] static Outcome parallel_with_render_load(Context& context);

    [[nodiscard]] static Outcome memory_pressure_host(Context& context);

    [[nodiscard]] static Outcome memory_pressure_guest(Context& context);

    [[nodiscard]] static Outcome sustained_30s(Context& context);
};
}  // namespace lsw::suites::sanity::data
