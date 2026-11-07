#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class ImageTransferChecks {
public:
    [[nodiscard]] static Outcome transfers_rect_fill(Context& context);

    [[nodiscard]] static Outcome from_buffer(Context& context);
};
}  // namespace lsw::suites::sanity::cl
