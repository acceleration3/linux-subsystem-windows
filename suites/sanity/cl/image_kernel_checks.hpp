#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class ImageKernelChecks {
public:
    [[nodiscard]] static Outcome images_in_kernels(Context& context);
};
}  // namespace lsw::suites::sanity::cl
