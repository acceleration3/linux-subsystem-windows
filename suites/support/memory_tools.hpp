#pragma once

#include "support/backend_harness.hpp"

namespace lsw::suites {
class MemoryTools {
public:
    [[nodiscard]] static bool touch(GpuBackend& backend, GpuBuffer& buffer, std::uint64_t seed, std::string& error);

    [[nodiscard]] static bool verify_ends(GpuBackend& backend, GpuBuffer& buffer, std::uint64_t seed, Context& context,
                                          std::string& error);

    [[nodiscard]] static std::uint64_t mib(std::uint64_t value) { return value << 20; }

    [[nodiscard]] static std::string api_label(const GpuBackend& backend) { return backend.name(); }
};
}  // namespace lsw::suites
