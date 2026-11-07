#pragma once

#include "support/bitmap.hpp"

#include <dxgi1_2.h>

#include <cstdint>
#include <vector>

namespace lsw::suites {
struct DuplicationFrame {
    DXGI_OUTDUPL_FRAME_INFO info{};
    Bitmap image;
    std::vector<RECT> dirty;
    std::vector<DXGI_OUTDUPL_MOVE_RECT> moves;
    bool has_shape = false;
    DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{};
    std::vector<std::uint8_t> shape_bytes;
    bool all_zero = false;
};
}  // namespace lsw::suites
