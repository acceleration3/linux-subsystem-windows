#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define LSW_GLAPI __stdcall
#else
#define LSW_GLAPI
#endif

namespace lsw::render::gl {
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLbyte = signed char;
using GLshort = short;
using GLint = int;
using GLsizei = int;
using GLubyte = unsigned char;
using GLushort = unsigned short;
using GLuint = unsigned int;
using GLfloat = float;
using GLdouble = double;
using GLchar = char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;
using GLint64 = long long;
using GLuint64 = unsigned long long;
using GLhalf = unsigned short;
using GLclampf = float;
using GLsync = struct __GLsync*;
using GLDEBUGPROC = void(LSW_GLAPI*)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar*, const void*);

namespace glc {
#include "render/gl/enums.inc"
constexpr GLuint64 TIMEOUT_IGNORED_ = 0xFFFFFFFFFFFFFFFFull;
}  // namespace glc
}  // namespace lsw::render::gl
