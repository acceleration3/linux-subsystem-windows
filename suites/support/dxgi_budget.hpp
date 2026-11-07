#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace lsw::suites {
struct VideoMemory {
    std::uint64_t budget = 0;
    std::uint64_t usage = 0;
    std::uint64_t available_for_reservation = 0;
    std::uint64_t reservation = 0;
};

class DxgiBudget {
public:
    [[nodiscard]] static bool query(const LUID& luid, bool local, VideoMemory& out, std::string& error);

    [[nodiscard]] static long set_reservation(const LUID& luid, bool local, std::uint64_t bytes);
};
}  // namespace lsw::suites
