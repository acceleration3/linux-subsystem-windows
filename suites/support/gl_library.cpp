#include "support/gl_library.hpp"

#include <cstdint>

namespace lsw::suites {

GlLibrary& GlLibrary::instance() {
    static GlLibrary* library = new GlLibrary();
    return *library;
}

bool GlLibrary::ready(std::string& error) {
    std::lock_guard lock(mutex_);
    if (loaded_) {
        return true;
    }
    if (!lib_.load("opengl32.dll")) {
        error = "opengl32.dll cannot be loaded";
        return false;
    }
    create_ = lib_.symbol<decltype(create_)>("wglCreateContext");
    delete_ = lib_.symbol<decltype(delete_)>("wglDeleteContext");
    make_current_ = lib_.symbol<decltype(make_current_)>("wglMakeCurrent");
    current_context_ = lib_.symbol<decltype(current_context_)>("wglGetCurrentContext");
    current_dc_ = lib_.symbol<decltype(current_dc_)>("wglGetCurrentDC");
    share_ = lib_.symbol<decltype(share_)>("wglShareLists");
    get_proc_ = lib_.symbol<decltype(get_proc_)>("wglGetProcAddress");
    if (create_ == nullptr || delete_ == nullptr || make_current_ == nullptr || current_context_ == nullptr ||
        current_dc_ == nullptr || share_ == nullptr || get_proc_ == nullptr) {
        error = "opengl32.dll lacks the WGL entry points";
        return false;
    }
    loaded_ = true;
    return true;
}

void* GlLibrary::proc(const char* name) const {
    if (get_proc_ == nullptr) {
        return nullptr;
    }
    void* address = reinterpret_cast<void*>(get_proc_(name));
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    if (value <= 3 || value == static_cast<std::uintptr_t>(-1)) {
        return nullptr;
    }
    return address;
}

void* GlLibrary::resolve(const char* name) const {
    void* address = proc(name);
    return address != nullptr ? address : export_symbol(name);
}

}  // namespace lsw::suites
