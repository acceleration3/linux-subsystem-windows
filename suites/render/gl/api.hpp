#pragma once

#include "render/gl/defs.hpp"

#include <functional>
#include <string>

#include "render/gl/functions.inc"

namespace lsw::render::gl {
class Api {
public:
#define LSW_GL_MEMBER(ret, name, ...) ret(LSW_GLAPI* name)(__VA_ARGS__) = nullptr;
    LSW_GL_FUNCTIONS(LSW_GL_MEMBER)
#undef LSW_GL_MEMBER

    [[nodiscard]] bool load(const std::function<void*(const char*)>& get_proc, std::string& error);

    [[nodiscard]] static std::string error_name(unsigned code);
};
}  // namespace lsw::render::gl
