#pragma once

#include "support/gl_rig.hpp"

namespace lsw::suites {
class GlSatellite {
public:
    [[nodiscard]] bool open(GlRig& rig, bool share, int client_width, int client_height, std::string& error);

    [[nodiscard]] bool bind(std::string& error);

    void unbind();

    void destroy();

    TestWindow window;
    GlContext context;
    GlApi gl;
};
}  // namespace lsw::suites
