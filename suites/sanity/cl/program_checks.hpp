#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class ProgramChecks {
public:
    [[nodiscard]] static Outcome build_source_options(Context& context);

    [[nodiscard]] static Outcome build_errors_logs(Context& context);

    [[nodiscard]] static Outcome binary_roundtrip(Context& context);

    [[nodiscard]] static Outcome il_spirv_caps(Context& context);

    [[nodiscard]] static Outcome compile_link(Context& context);
};
}  // namespace lsw::suites::sanity::cl
