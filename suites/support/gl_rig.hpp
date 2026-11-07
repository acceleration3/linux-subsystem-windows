#pragma once

#include "core/context.hpp"
#include "support/av_adapters.hpp"
#include "support/gl_api.hpp"
#include "support/gl_context.hpp"
#include "support/test_window.hpp"
#include "support/wgl_api.hpp"

#include <memory>
#include <string>

namespace lsw::suites {
struct GlRigOptions {
    int width = 256;
    int height = 256;
    bool visible = true;
    GlContextRequest request;
};

class GlSatellite;

class GlRig {
public:
    GlRig() = default;
    GlRig(const GlRig&) = delete;
    GlRig& operator=(const GlRig&) = delete;

    [[nodiscard]] bool open(const GlRigOptions& options, std::string& error);

    [[nodiscard]] TestWindowConfig window_config(int client_width, int client_height, bool visible) const;

    [[nodiscard]] bool make_current(std::string& error);

    void record(Context& check) const;

    [[nodiscard]] HDC dc() const { return window.dc(); }

    AvAdapter adapter;
    TestWindow window;
    GlContext context;
    GlApi gl;
    WglApi wgl;
    GlContextRequest request;
    PIXELFORMATDESCRIPTOR pfd{};
    int pixel_format = 0;
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string identity_note;
};
}  // namespace lsw::suites
