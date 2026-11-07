#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::capture {
class DupChecks {
public:
    [[nodiscard]] static Outcome desktop_duplication_content(Context& context);

    [[nodiscard]] static Outcome dirty_rects_cover_changes(Context& context);

    [[nodiscard]] static Outcome pointer_info(Context& context);
};
}  // namespace lsw::suites::sanity::capture
