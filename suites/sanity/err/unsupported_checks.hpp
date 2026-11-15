#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::err {
class UnsupportedChecks {
public:
    [[nodiscard]] static Outcome caps_match_use(Context& context);

    [[nodiscard]] static Outcome formats(Context& context);

    [[nodiscard]] static Outcome extensions_list_vs_entry(Context& context);

    [[nodiscard]] static Outcome opencl_version_coherent(Context& context);
};
}  // namespace lsw::suites::sanity::err
