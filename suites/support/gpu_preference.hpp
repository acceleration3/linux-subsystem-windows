#pragma once

#include "support/av_adapters.hpp"

namespace lsw::suites {
class GpuPreferenceScope {
public:
    static constexpr const char* kMarker = "LSW_SUITES_GPU_PREFERENCE";

    [[nodiscard]] static bool needed(const AvAdapter& target);

    [[nodiscard]] static int run_again(const AvAdapter& target);
};
}  // namespace lsw::suites
