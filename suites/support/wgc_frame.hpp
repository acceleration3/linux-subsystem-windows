#pragma once

#include "support/bitmap.hpp"

namespace lsw::suites {
struct WgcFrame {
    Bitmap image;
    int content_width = 0;
    int content_height = 0;
    long long system_time = 0;
};
}  // namespace lsw::suites
