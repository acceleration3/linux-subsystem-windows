#pragma once

#include "core/context.hpp"
#include "support/av_adapters.hpp"
#include "support/gpu_backend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites {
class BackendHarness {
public:
    static constexpr std::uint64_t kWindow = 16ull << 20;

    [[nodiscard]] static std::unique_ptr<GpuBackend> open(const std::string& api, AvAdapter& adapter,
                                                          std::string& error);

    [[nodiscard]] static std::vector<std::uint64_t> boundary_sizes();

    [[nodiscard]] static std::vector<std::uint64_t> small_sizes();

    [[nodiscard]] static bool roundtrip(GpuBackend& backend, std::uint64_t size, std::uint64_t seed,
                                        AllocStatus& status, Context& context, std::string& error);

    [[nodiscard]] static bool verify(Context& context, const std::string& label, const void* expected,
                                     const void* actual, std::uint64_t size, std::string& error);

    [[nodiscard]] static bool fill_and_verify(GpuBackend& backend, GpuBuffer& buffer, std::uint64_t seed,
                                              std::uint64_t size, Context& context, std::string& error);
};
}  // namespace lsw::suites
