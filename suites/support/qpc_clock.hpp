#pragma once

#include <windows.h>

namespace lsw::suites {
class QpcClock {
public:
    [[nodiscard]] static long long now() {
        LARGE_INTEGER value;
        ::QueryPerformanceCounter(&value);
        return value.QuadPart;
    }

    [[nodiscard]] static long long frequency() {
        static const long long hz = [] {
            LARGE_INTEGER value;
            ::QueryPerformanceFrequency(&value);
            return value.QuadPart;
        }();
        return hz;
    }

    [[nodiscard]] static double ms(long long ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency()); }

    [[nodiscard]] static double ms_since(long long start) { return ms(now() - start); }
};
}  // namespace lsw::suites
