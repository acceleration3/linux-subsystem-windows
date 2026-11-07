#include "sanity/present/chain_probe.hpp"

#include "support/frame_image.hpp"
#include "support/screen_capture.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>

namespace lsw::suites::sanity::present {

bool ChainProbe::within(std::uint32_t a, std::uint32_t b, int tolerance) {
    for (int shift = 0; shift < 24; shift += 8) {
        const int delta = static_cast<int>((a >> shift) & 0xFFu) - static_cast<int>((b >> shift) & 0xFFu);
        if (std::abs(delta) > tolerance) {
            return false;
        }
    }
    return true;
}

bool ChainProbe::quadrants(const Bitmap& image, const RECT& content, int frame, int tolerance, std::string& detail) {
    const int width = static_cast<int>(content.right - content.left);
    const int height = static_cast<int>(content.bottom - content.top);
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const RECT r = FrameImage::quadrant(width, height, quadrant);
        const int x = static_cast<int>(content.left) + FrameImage::centre_x(r);
        const int y = static_cast<int>(content.top) + FrameImage::centre_y(r);
        const std::uint32_t want = FrameImage::color(frame, quadrant) & 0xFFFFFFu;
        if (!image.valid(x, y)) {
            detail = "quadrant " + std::to_string(quadrant) + " centre (" + std::to_string(x) + "," + std::to_string(y) + ") is outside the capture";
            return false;
        }
        const std::uint32_t got = image.at(x, y);
        if (!within(got, want, tolerance)) {
            detail = "quadrant " + std::to_string(quadrant) + " at (" + std::to_string(x) + "," + std::to_string(y) + ") is " + ScreenCapture::rgb_text(got) + ", expected " +
                     ScreenCapture::rgb_text(want) + " within " + std::to_string(tolerance);
            return false;
        }
    }
    return true;
}

bool ChainProbe::poll_quadrants(const RECT& client, const RECT& content_in_client, int frame, int tolerance, int timeout_ms, std::string& detail) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    Bitmap image;
    do {
        if (!ScreenCapture::screen(client, false, image, detail)) {
            return false;
        }
        if (quadrants(image, content_in_client, frame, tolerance, detail)) {
            return true;
        }
        ::Sleep(15);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

float ChainProbe::linear_of(int value8) {
    const double c = static_cast<double>(value8) / 255.0;
    return static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
}

}  // namespace lsw::suites::sanity::present
