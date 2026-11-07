#pragma once

#include <cstdint>
#include <vector>

namespace lsw::suites {
class ClSpirv {
public:
    static constexpr const char* kAddKernel = "lsw_add";

    [[nodiscard]] static std::vector<std::uint32_t> add_module(unsigned address_bits);
};
}  // namespace lsw::suites
