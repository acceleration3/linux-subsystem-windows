#include "support/frame_probe.hpp"

#include "support/frame_image.hpp"
#include "support/screen_capture.hpp"

#include <string>

namespace lsw::suites {

namespace {

int frame_of_quadrant(int quadrant, std::uint32_t value, int tolerance) {
    for (int frame = 0; frame < FrameImage::kFrames; ++frame) {
        const std::uint32_t want = FrameImage::color(frame, quadrant);
        bool close_enough = true;
        for (int shift = 0; shift < 24; shift += 8) {
            const int delta = static_cast<int>((want >> shift) & 0xFFu) - static_cast<int>((value >> shift) & 0xFFu);
            close_enough = close_enough && delta <= tolerance && -delta <= tolerance;
        }
        if (close_enough) {
            return frame;
        }
    }
    return -1;
}

}  // namespace

int FrameProbe::decode(const Bitmap& image, int origin_x, int origin_y, int width, int height, int tolerance) {
    int frames[4];
    int known = 0;
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const RECT rect = FrameImage::quadrant(width, height, quadrant);
        const int x = origin_x + FrameImage::centre_x(rect);
        const int y = origin_y + FrameImage::centre_y(rect);
        frames[quadrant] = image.valid(x, y) ? frame_of_quadrant(quadrant, image.at(x, y), tolerance) : -1;
        known += frames[quadrant] >= 0 ? 1 : 0;
    }
    if (known == 0) {
        return kNone;
    }
    if (known == 4 && frames[0] == frames[1] && frames[1] == frames[2] && frames[2] == frames[3]) {
        return frames[0];
    }
    return kTorn;
}

int FrameProbe::sample(const RECT& area, int tolerance) {
    Bitmap image;
    std::string error;
    if (!ScreenCapture::screen(area, false, image, error)) {
        return kNone;
    }
    return decode(image, 0, 0, static_cast<int>(area.right - area.left), static_cast<int>(area.bottom - area.top), tolerance);
}

const char* FrameProbe::name(int value) {
    if (value == kNone) {
        return "no frame pattern";
    }
    return value == kTorn ? "pieces of different frames" : "a frame";
}

}  // namespace lsw::suites
