#pragma once

#include "render/host/guest_types.hpp"
#include "support/d3d11_tools.hpp"

#include <dxgi.h>

namespace lsw::suites::render {
class Convert {
public:
    [[nodiscard]] static bool kind_of(DXGI_FORMAT format, rr::PixelKind& kind);

    [[nodiscard]] static bool image_of(const TexelImage& texels, DXGI_FORMAT format, rr::Image& out, std::string& error);

    [[nodiscard]] static bool stencil_of(const TexelImage& texels, rr::Image& out, std::string& error);

    [[nodiscard]] static rr::Image from_bgra(const std::vector<std::uint8_t>& data, unsigned width, unsigned height);
};
}  // namespace lsw::suites::render
