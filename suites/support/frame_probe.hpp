#pragma once

#include "support/bitmap.hpp"

#include <windows.h>

namespace lsw::suites {
class FrameProbe {
public:
    static constexpr int kNone = -1;
    static constexpr int kTorn = -2;

    [[nodiscard]] static int decode(const Bitmap& image, int origin_x, int origin_y, int width, int height, int tolerance = 0);

    [[nodiscard]] static int sample(const RECT& area, int tolerance = 0);

    [[nodiscard]] static const char* name(int value);
};
}  // namespace lsw::suites
