#include "support/gl_satellite.hpp"

namespace lsw::suites {

bool GlSatellite::open(GlRig& rig, bool share, int client_width, int client_height, std::string& error) {
    if (!window.create(rig.window_config(client_width, client_height, false), error)) {
        return false;
    }
    if (::SetPixelFormat(window.dc(), rig.pixel_format, &rig.pfd) == FALSE) {
        error = "SetPixelFormat(" + std::to_string(rig.pixel_format) + ") failed on a second window (error " +
                std::to_string(::GetLastError()) + ")";
        return false;
    }
    return context.create(window.dc(), rig.request, share ? rig.context.handle() : nullptr, error);
}

bool GlSatellite::bind(std::string& error) {
    if (!context.make_current(window.dc())) {
        error = "wglMakeCurrent failed on a second context (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    return gl.load(GlLibrary::instance(), error);
}

void GlSatellite::unbind() {
    GlContext::release();
}

void GlSatellite::destroy() {
    context.destroy();
    window.destroy();
}

}  // namespace lsw::suites
