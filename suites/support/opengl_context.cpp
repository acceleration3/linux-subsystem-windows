#include "support/opengl_context.hpp"

#include "core/text.hpp"

namespace lsw::suites {

bool OpenGlContext::create(HDC dc, std::string& error) {
    destroy();
    if (!gl_.load("opengl32.dll")) {
        error = "opengl32.dll cannot be loaded";
        return false;
    }
    auto create_context = gl_.symbol<HGLRC(__stdcall*)(HDC)>("wglCreateContext");
    delete_context_ = gl_.symbol<HGLRC(__stdcall*)(HGLRC)>("wglDeleteContext");
    make_current_ = gl_.symbol<BOOL(__stdcall*)(HDC, HGLRC)>("wglMakeCurrent");
    get_proc_ = gl_.symbol<PROC(__stdcall*)(LPCSTR)>("wglGetProcAddress");
    get_string_ = gl_.symbol<const unsigned char*(__stdcall*)(unsigned)>("glGetString");
    if (create_context == nullptr || delete_context_ == nullptr || make_current_ == nullptr ||
        get_proc_ == nullptr || get_string_ == nullptr) {
        error = "opengl32.dll lacks the WGL entry points";
        return false;
    }
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    const int format = ::ChoosePixelFormat(dc, &pfd);
    if (format == 0 || ::SetPixelFormat(dc, format, &pfd) == FALSE) {
        error = "no OpenGL pixel format on the window DC (error " +
                std::to_string(::GetLastError()) + ")";
        return false;
    }
    context_ = create_context(dc);
    if (context_ == nullptr) {
        error = "wglCreateContext failed (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    if (make_current_(dc, context_) == FALSE) {
        error = "wglMakeCurrent failed (error " + std::to_string(::GetLastError()) + ")";
        delete_context_(context_);
        context_ = nullptr;
        return false;
    }
    dc_ = dc;
    return true;
}

void OpenGlContext::destroy() {
    if (context_ != nullptr) {
        make_current_(nullptr, nullptr);
        delete_context_(context_);
        context_ = nullptr;
    }
    dc_ = nullptr;
    gl_.unload();
}

std::string OpenGlContext::get_string(unsigned name) const {
    if (context_ == nullptr) {
        return {};
    }
    const unsigned char* text = get_string_(name);
    return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
}

std::string OpenGlContext::wgl_extensions() const {
    if (context_ == nullptr) {
        return {};
    }
    using GetExtensions = const char*(__stdcall*)(HDC);
    void* arb = reinterpret_cast<void*>(get_proc_("wglGetExtensionsStringARB"));
    if (arb == nullptr) {
        return {};
    }
    const char* text = reinterpret_cast<GetExtensions>(arb)(dc_);
    return text == nullptr ? std::string() : std::string(text);
}

}  // namespace lsw::suites
