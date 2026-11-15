#pragma once

#include <cstdint>

namespace lsw::suites {
struct FrameStats {
    std::uint64_t present_count = 0;
    std::uint64_t present_refresh_count = 0;
    std::uint64_t sync_refresh_count = 0;
    long long sync_qpc = 0;
};
}  // namespace lsw::suites
