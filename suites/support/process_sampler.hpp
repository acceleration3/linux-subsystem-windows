#pragma once

#include <cstdint>

namespace lsw::suites {
struct ProcessSample {
    std::uint64_t private_bytes = 0;
    std::uint32_t handles = 0;
};

class ProcessSampler {
public:
    [[nodiscard]] static ProcessSample take();

    [[nodiscard]] static std::uint64_t available_physical();
};
}  // namespace lsw::suites
