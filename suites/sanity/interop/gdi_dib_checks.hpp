#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::interop {
class GdiDibChecks {
public:
    [[nodiscard]] static Outcome section_upload(Context& context);

    [[nodiscard]] static Outcome after_gpu_writes(Context& context);

    [[nodiscard]] static Outcome multi_thread(Context& context);
};
}  // namespace lsw::suites::sanity::interop
