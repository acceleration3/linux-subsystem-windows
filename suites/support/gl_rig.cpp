#include "support/gl_rig.hpp"

#include "core/text.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstdlib>

namespace lsw::suites {

namespace {

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && text[start] == ' ') {
        ++start;
    }
    return text.substr(start);
}

bool names_adapter(const std::string& renderer, const std::string& adapter) {
    const std::string upper_renderer = Text::upper(renderer);
    const std::string upper_adapter = Text::upper(trimmed(adapter));
    return !upper_renderer.empty() && !upper_adapter.empty() &&
           (upper_renderer.find(upper_adapter) != std::string::npos ||
            upper_adapter.find(upper_renderer) != std::string::npos);
}

}  // namespace

TestWindowConfig GlRig::window_config(int client_width, int client_height, bool visible) const {
    TestWindowConfig config;
    config.visible = visible;
    for (const AvOutput& output : adapter.outputs) {
        if (output.attached) {
            config.x = output.rect.left + 20;
            config.y = output.rect.top + 20;
            break;
        }
    }
    TestWindow::outer_size(config.x, config.y, client_width, client_height, config.width, config.height);
    return config;
}

bool GlRig::open(const GlRigOptions& options, std::string& error) {
    if (!GlLibrary::instance().ready(error) || !TargetAdapter::pick(adapter, error)) {
        return false;
    }
    request = options.request;
    if (!window.create(window_config(options.width, options.height, options.visible), error)) {
        return false;
    }
    PIXELFORMATDESCRIPTOR want{};
    want.nSize = sizeof(want);
    want.nVersion = 1;
    want.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    want.iPixelType = PFD_TYPE_RGBA;
    want.cColorBits = 32;
    want.cAlphaBits = 8;
    want.cDepthBits = 24;
    want.cStencilBits = 8;
    pixel_format = ::ChoosePixelFormat(window.dc(), &want);
    if (pixel_format == 0 || ::DescribePixelFormat(window.dc(), pixel_format, sizeof(pfd), &pfd) == 0) {
        error = "no OpenGL pixel format on the window DC (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    if ((pfd.dwFlags & PFD_GENERIC_FORMAT) != 0 && (pfd.dwFlags & PFD_GENERIC_ACCELERATED) == 0) {
        error = "ChoosePixelFormat selected format " + std::to_string(pixel_format) +
                ", Microsoft's generic software format, on the output of " + adapter.name;
        return false;
    }
    if (::SetPixelFormat(window.dc(), pixel_format, &pfd) == FALSE) {
        error = "SetPixelFormat(" + std::to_string(pixel_format) + ") failed (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    if (!context.create(window.dc(), request, nullptr, error)) {
        return false;
    }
    if (!make_current(error)) {
        return false;
    }
    return true;
}

bool GlRig::make_current(std::string& error) {
    if (!context.make_current(window.dc())) {
        error = "wglMakeCurrent failed (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    if (!gl.load(GlLibrary::instance(), error)) {
        return false;
    }
    wgl.load(GlLibrary::instance());
    vendor = gl.text(glc::VENDOR);
    renderer = gl.text(glc::RENDERER);
    version = gl.text(glc::VERSION);
    if (std::getenv("LSW_SUITES_DEV_ANY") != nullptr) {
        identity_note = "identity not enforced (LSW_SUITES_DEV_ANY)";
        return true;
    }
    if (!names_adapter(renderer, adapter.name)) {
        error = "the OpenGL ICD that loaded for the window on " + adapter.name + " is not the adapter's: GL_VENDOR '" +
                vendor + "', GL_RENDERER '" + renderer + "', GL_VERSION '" + version + "'";
        return false;
    }
    return true;
}

void GlRig::record(Context& check) const {
    check.measure("gl.vendor", vendor);
    check.measure("gl.renderer", renderer);
    check.measure("gl.version", version);
    check.measure("gl.pixel_format", pixel_format);
    if (!identity_note.empty()) {
        check.note(identity_note);
    }
}

}  // namespace lsw::suites
