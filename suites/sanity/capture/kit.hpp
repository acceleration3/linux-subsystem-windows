#pragma once

#include "core/context.hpp"
#include "support/bitmap.hpp"
#include "support/screen_capture.hpp"

#include <string>
#include <vector>

namespace lsw::suites::sanity::capture {
class Kit {
public:
    static constexpr int kSide = 128;

    [[nodiscard]] static bool region_matches(const Bitmap& image, int origin_x, int origin_y, int width, int height, const std::vector<std::uint32_t>& want, std::string& where);

    [[nodiscard]] static bool find_region(const Bitmap& image, const std::vector<std::uint32_t>& want, int width, int height, POINT& found);

    static void keep(Context& context, const std::string& name, const Bitmap& image, int origin_x, int origin_y, int width, int height, const std::vector<std::uint32_t>& want);

    [[nodiscard]] static std::string hr(HRESULT result);

    [[nodiscard]] static std::string sanitize(std::string text);
};
}  // namespace lsw::suites::sanity::capture
