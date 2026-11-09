#include "render/d3d/convert.hpp"

#include <cstring>

namespace lsw::suites::render {

bool Convert::kind_of(DXGI_FORMAT format, rr::PixelKind& kind) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            kind = rr::PixelKind::Rgba8Unorm;
            return true;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            kind = rr::PixelKind::Bgra8Unorm;
            return true;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            kind = rr::PixelKind::Rgba16Float;
            return true;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            kind = rr::PixelKind::Rgba32Float;
            return true;
        case DXGI_FORMAT_R32_FLOAT:
            kind = rr::PixelKind::R32Float;
            return true;
        case DXGI_FORMAT_D32_FLOAT:
            kind = rr::PixelKind::Depth32Float;
            return true;
        case DXGI_FORMAT_R32G32B32A32_UINT:
            kind = rr::PixelKind::Rgba32Uint;
            return true;
        case DXGI_FORMAT_R8_UINT:
            kind = rr::PixelKind::R8Uint;
            return true;
        default:
            return false;
    }
}

bool Convert::image_of(const TexelImage& texels, DXGI_FORMAT format, rr::Image& out, std::string& error) {
    rr::PixelKind kind;
    if (!kind_of(format, kind)) {
        error = "no image kind for DXGI format " + std::to_string(static_cast<int>(format));
        return false;
    }
    rr::Image image(texels.width, texels.height, kind);
    if (texels.row_bytes != image.stride() || texels.data.size() != image.bytes().size()) {
        error = "texel readback size does not match the image kind";
        return false;
    }
    std::memcpy(image.bytes().data(), texels.data.data(), image.bytes().size());
    out = std::move(image);
    return true;
}

bool Convert::stencil_of(const TexelImage& texels, rr::Image& out, std::string& error) {
    if (texels.row_bytes != texels.width * 4) {
        error = "stencil readback expects a 32-bit depth-stencil format";
        return false;
    }
    rr::Image image(texels.width, texels.height, rr::PixelKind::R8Uint);
    for (UINT y = 0; y < texels.height; ++y) {
        for (UINT x = 0; x < texels.width; ++x) {
            image.row(y)[x] = texels.data[(static_cast<std::size_t>(y) * texels.width + x) * 4 + 3];
        }
    }
    out = std::move(image);
    return true;
}

rr::Image Convert::from_bgra(const std::vector<std::uint8_t>& data, unsigned width, unsigned height) {
    rr::Image image(width, height, rr::PixelKind::Bgra8Unorm);
    std::memcpy(image.bytes().data(), data.data(), std::min(data.size(), image.bytes().size()));
    return image;
}

}  // namespace lsw::suites::render
