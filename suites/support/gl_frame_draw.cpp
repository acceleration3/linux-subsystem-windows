#include "support/gl_frame_draw.hpp"

#include "support/frame_image.hpp"

namespace lsw::suites {

void GlFrameDraw::frame(const GlApi& api, int width, int height, int index) {
    api.glViewport(0, 0, width, height);
    api.glEnable(glc::SCISSOR_TEST);
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const std::uint32_t c = FrameImage::color(index, quadrant);
        const RECT r = FrameImage::quadrant(width, height, quadrant);
        api.glScissor(r.left, height - r.bottom, r.right - r.left, r.bottom - r.top);
        api.glClearColor(static_cast<float>((c >> 16) & 255u) / 255.0f, static_cast<float>((c >> 8) & 255u) / 255.0f, static_cast<float>(c & 255u) / 255.0f, 1.0f);
        api.glClear(glc::COLOR_BUFFER_BIT);
    }
    api.glDisable(glc::SCISSOR_TEST);
}

}  // namespace lsw::suites
