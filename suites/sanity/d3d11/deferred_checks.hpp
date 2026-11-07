#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class DeferredChecks {
public:
    [[nodiscard]] static Outcome equivalence(Context& context);

    [[nodiscard]] static Outcome maps(Context& context);

    [[nodiscard]] static Outcome list_reuse(Context& context);

    [[nodiscard]] static Outcome clearstate(Context& context);

    [[nodiscard]] static Outcome free_threaded(Context& context);

    [[nodiscard]] static Outcome multithread_protect(Context& context);

    [[nodiscard]] static Outcome two_devices(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
