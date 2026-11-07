#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GdiD2dChecks {
public:
    [[nodiscard]] static Outcome gdi_interop_target(Context& context);

    [[nodiscard]] static Outcome dwrite_text_on_surface(Context& context);
};
}  // namespace lsw::suites::sanity::interop
