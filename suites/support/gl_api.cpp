#include "support/gl_api.hpp"

#include "core/text.hpp"

#include <cstdio>
#include <sstream>

namespace lsw::suites {

bool GlApi::load(const GlLibrary& library, std::string& error) {
#define LSW_GL_LOAD(ret, name, ...) name = reinterpret_cast<decltype(name)>(library.resolve(#name));
    LSW_GL_FUNCTIONS(LSW_GL_LOAD)
#undef LSW_GL_LOAD
    if (glGetString == nullptr || glGetError == nullptr || glGetIntegerv == nullptr) {
        error = "the OpenGL library lacks glGetString, glGetError or glGetIntegerv";
        return false;
    }
    const std::string version_text = text(glc::VERSION);
    if (version_text.empty()) {
        error = "glGetString(GL_VERSION) returned nothing on the current context";
        return false;
    }
    if (std::sscanf(version_text.c_str(), "%d.%d", &major_, &minor_) != 2) {
        error = "GL_VERSION '" + version_text + "' does not start with major.minor";
        return false;
    }
    extensions_.clear();
    while (glGetError() != 0) {
    }
    bool listed = false;
    if (major_ >= 3 && glGetStringi != nullptr) {
        const int count = integer(glc::NUM_EXTENSIONS);
        for (int i = 0; i < count; ++i) {
            const unsigned char* name = glGetStringi(glc::EXTENSIONS, static_cast<unsigned>(i));
            if (name != nullptr) {
                extensions_.insert(reinterpret_cast<const char*>(name));
            }
        }
        listed = glGetError() == 0 && count > 0;
    }
    if (!listed) {
        std::istringstream stream(text(glc::EXTENSIONS));
        std::string word;
        while (stream >> word) {
            extensions_.insert(word);
        }
    }
    while (glGetError() != 0) {
    }
    return true;
}

std::string GlApi::text(unsigned name) const {
    const unsigned char* value = glGetString(name);
    return value == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(value));
}

int GlApi::integer(unsigned name) const {
    int value = 0;
    glGetIntegerv(name, &value);
    return value;
}

unsigned GlApi::drain_errors() const {
    unsigned first = 0;
    for (int i = 0; i < 64; ++i) {
        const unsigned error = glGetError();
        if (error == 0) {
            break;
        }
        if (first == 0) {
            first = error;
        }
    }
    return first;
}

std::string GlApi::error_name(unsigned code) {
    switch (code) {
    case glc::NO_ERROR_: return "GL_NO_ERROR";
    case glc::INVALID_ENUM: return "GL_INVALID_ENUM";
    case glc::INVALID_VALUE: return "GL_INVALID_VALUE";
    case glc::INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case glc::STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
    case glc::STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
    case glc::OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    case glc::INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    default: return "GL error 0x" + Text::hex(code, 4);
    }
}

}  // namespace lsw::suites
