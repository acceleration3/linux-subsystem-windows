#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct DxgiAdapter {
    std::string name;
    std::uint32_t luid_low = 0;
    std::int32_t luid_high = 0;
    std::uint32_t vendor = 0;
    std::uint32_t device = 0;
    std::uint32_t subsystem = 0;

    [[nodiscard]] std::string luid() const;
};

class DxgiAdapters {
public:
    [[nodiscard]] static bool enumerate(std::vector<DxgiAdapter>& out, std::string& error);
};
}  // namespace lsw::suites
