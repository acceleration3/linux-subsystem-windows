#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "core/text.hpp"
#include "support/artifact_writer.hpp"
#include "support/bitmap.hpp"
#include "support/com_ptr.hpp"
#include "support/frame_image.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/screen_capture.hpp"
#include "support/target_adapter.hpp"
#include "support/test_window.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites::sanity::interop {
class Kit {
public:
    template <typename U>
    [[nodiscard]] static Com<U> query(IUnknown* object) {
        U* other = nullptr;
        if (object != nullptr) {
            object->QueryInterface(__uuidof(U), reinterpret_cast<void**>(&other));
        }
        return Com<U>(other);
    }

    [[nodiscard]] static std::size_t at(int x, int y, int width) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    }

    [[nodiscard]] static std::string hex(std::uint64_t value, int digits = 8) { return Text::hex(value, digits); }

    [[nodiscard]] static std::string hr(HRESULT result) { return "0x" + Text::hex(static_cast<std::uint32_t>(result), 8); }

    [[nodiscard]] static std::string rgb(std::uint32_t value) { return ScreenCapture::rgb_text(value); }

    [[nodiscard]] static std::uint32_t pack(int r, int g, int b) {
        return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
    }

    [[nodiscard]] static COLORREF colorref(std::uint32_t xrgb) {
        return RGB((xrgb >> 16) & 255u, (xrgb >> 8) & 255u, xrgb & 255u);
    }

    static void keep_image(Context& context, const std::string& name, const Bitmap& image);

    static void keep_pixels(Context& context, const std::string& name, const std::vector<std::uint32_t>& pixels, int width, int height);

    static bool judge(Context& context, Problems& problems, const std::string& label, const Bitmap& got, int origin_x, int origin_y,
                      int width, int height, const std::vector<std::uint32_t>& expected, int tolerance);

    [[nodiscard]] static bool settle_screen(Context& context, const std::string& label, const RECT& area, const std::vector<std::uint32_t>& expected,
                                            int tolerance, int timeout_ms, std::string& detail);

    [[nodiscard]] static std::vector<std::uint32_t> uniform(int width, int height, std::uint32_t xrgb);

    static void paint(std::vector<std::uint32_t>& image, int width, const RECT& rect, std::uint32_t xrgb);

    [[nodiscard]] static std::vector<std::uint32_t> blend(const std::vector<std::uint32_t>& below, const std::vector<std::uint32_t>& premultiplied,
                                                          const std::vector<std::uint8_t>& alpha);

    [[nodiscard]] static std::string sanitize(std::string text);

    [[nodiscard]] static std::uint64_t seed(int a, int b = 0) { return 0x1A7E0000ull + static_cast<std::uint64_t>(a) * 977u + static_cast<std::uint64_t>(b); }

    [[nodiscard]] static std::vector<std::uint8_t> frame_bytes(std::uint64_t base, int frame, int width, int height);
};
}  // namespace lsw::suites::sanity::interop
