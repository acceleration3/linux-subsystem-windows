#pragma once

#include <cstdint>
#include <windows.h>

namespace lsw::agent::win {
class FileTime {
public:
    FileTime() = delete;

    [[nodiscard]] static std::uint64_t to_u64(const FILETIME& ft) noexcept {
        return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    [[nodiscard]] static std::uint64_t to_unix_ms(const FILETIME& ft) noexcept {
        const std::uint64_t ticks = to_u64(ft);
        return ticks <= kUnixEpochTicks ? 0 : (ticks - kUnixEpochTicks) / kTicksPerMs;
    }

    [[nodiscard]] static FILETIME from_unix_ms(std::uint64_t ms) noexcept {
        const std::uint64_t ticks = ms * kTicksPerMs + kUnixEpochTicks;
        FILETIME ft{};
        ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
        ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
        return ft;
    }

private:
    static constexpr std::uint64_t kUnixEpochTicks = 116444736000000000ULL;
    static constexpr std::uint64_t kTicksPerMs = 10000ULL;
};

}
