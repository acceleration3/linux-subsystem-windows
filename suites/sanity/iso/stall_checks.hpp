#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::iso {
class StallChecks {
public:
    [[nodiscard]] static Outcome heavy_load_other_finishes(Context& context);

    [[nodiscard]] static Outcome huge_upload_other_progress(Context& context);
};
}  // namespace lsw::suites::sanity::iso
