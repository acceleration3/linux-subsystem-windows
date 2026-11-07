#include "sanity/capture/kit.hpp"

#include "sanity/interop/kit.hpp"

namespace lsw::suites::sanity::capture {

bool Kit::region_matches(const Bitmap& image, int origin_x, int origin_y, int width, int height, const std::vector<std::uint32_t>& want, std::string& where) {
    return ScreenCapture::matches_region(image, origin_x, origin_y, width, height, want, 0, where);
}

bool Kit::find_region(const Bitmap& image, const std::vector<std::uint32_t>& want, int width, int height, POINT& found) {
    if (want.empty()) {
        return false;
    }
    std::string ignored;
    for (int y = 0; y + height <= image.height; ++y) {
        for (int x = 0; x + width <= image.width; ++x) {
            if (image.at(x, y) == (want[0] & 0xFFFFFFu) && ScreenCapture::matches_region(image, x, y, width, height, want, 0, ignored)) {
                found = {x, y};
                return true;
            }
        }
    }
    return false;
}

void Kit::keep(Context& context, const std::string& name, const Bitmap& image, int origin_x, int origin_y, int width, int height, const std::vector<std::uint32_t>& want) {
    Bitmap region;
    region.width = width;
    region.height = height;
    region.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            region.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = image.valid(origin_x + x, origin_y + y) ? image.at(origin_x + x, origin_y + y) : 0u;
        }
    }
    interop::Kit::keep_image(context, name + ".actual", region);
    interop::Kit::keep_pixels(context, name + ".expected", want, width, height);
}

std::string Kit::hr(HRESULT result) {
    return interop::Kit::hr(result);
}

std::string Kit::sanitize(std::string text) {
    return interop::Kit::sanitize(std::move(text));
}

}  // namespace lsw::suites::sanity::capture
