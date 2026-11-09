#include "render/gl/api.hpp"

namespace lsw::render::gl {

bool Api::load(const std::function<void*(const char*)>& get_proc, std::string& error) {
#define LSW_GL_LOAD(ret, name, ...)                                                        \
    name = reinterpret_cast<decltype(name)>(get_proc(#name));                              \
    if (name == nullptr) {                                                                 \
        error = "missing OpenGL function " #name;                                          \
        return false;                                                                      \
    }
    LSW_GL_FUNCTIONS(LSW_GL_LOAD)
#undef LSW_GL_LOAD
    return true;
}

std::string Api::error_name(unsigned code) {
    switch (code) {
        case 0x0500: return "GL_INVALID_ENUM";
        case 0x0501: return "GL_INVALID_VALUE";
        case 0x0502: return "GL_INVALID_OPERATION";
        case 0x0505: return "GL_OUT_OF_MEMORY";
        case 0x0506: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        default: return "GL error " + std::to_string(code);
    }
}

}  // namespace lsw::render::gl
