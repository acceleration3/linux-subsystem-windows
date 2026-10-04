#pragma once

#include <cstdint>
#include <windows.h>

namespace lsw::agent::win {

// FILETIME arithmetic: 100-nanosecond ticks since 1601-01-01 UTC, split
// across two 32-bit halves.
class FileTime {
public:
    FileTime() = delete;

    // The two halves as one tick count.
    [[nodiscard]] static std::uint64_t to_u64(const FILETIME& ft) noexcept {
        return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    // Milliseconds since the Unix epoch. 0 for a time at or before it —
    // which includes the all-zero FILETIME a file system reports when it
    // keeps no timestamp — rather than an unsigned wrap-around.
    [[nodiscard]] static std::uint64_t to_unix_ms(const FILETIME& ft) noexcept {
        const std::uint64_t ticks = to_u64(ft);
        return ticks <= kUnixEpochTicks ? 0 : (ticks - kUnixEpochTicks) / kTicksPerMs;
    }

    // The inverse of ``to_unix_ms``.
    [[nodiscard]] static FILETIME from_unix_ms(std::uint64_t ms) noexcept {
        const std::uint64_t ticks = ms * kTicksPerMs + kUnixEpochTicks;
        FILETIME ft{};
        ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
        ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
        return ft;
    }

private:
    // 1970-01-01 is 11644473600 seconds after 1601-01-01.
    static constexpr std::uint64_t kUnixEpochTicks = 116444736000000000ULL;
    static constexpr std::uint64_t kTicksPerMs = 10000ULL;
};

}  // namespace lsw::agent::win
