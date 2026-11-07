#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GdiCaptureChecks {
public:
    [[nodiscard]] static Outcome bitblt_each_api(Context& context);

    [[nodiscard]] static Outcome printwindow_full_content(Context& context);

    [[nodiscard]] static Outcome cursor_in_captures(Context& context);
};
}  // namespace lsw::suites::sanity::interop
