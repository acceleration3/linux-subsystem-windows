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

namespace glc {
constexpr GLenum COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum DEPTH_BUFFER_BIT = 0x0100;
constexpr GLenum STENCIL_BUFFER_BIT = 0x0400;
constexpr GLenum FALSE_ = 0;
constexpr GLenum TRUE_ = 1;
constexpr GLenum TRIANGLES = 0x0004;
constexpr GLenum TRIANGLE_STRIP = 0x0005;
constexpr GLenum TRIANGLE_FAN = 0x0006;
constexpr GLenum LINES = 0x0001;
constexpr GLenum POINTS = 0x0000;
constexpr GLenum SRC_ALPHA = 0x0302;
constexpr GLenum ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum ONE = 1;
constexpr GLenum ZERO = 0;
constexpr GLenum DEPTH_TEST = 0x0B71;
constexpr GLenum STENCIL_TEST = 0x0B90;
constexpr GLenum BLEND = 0x0BE2;
constexpr GLenum CULL_FACE = 0x0B44;
constexpr GLenum SCISSOR_TEST = 0x0C11;
constexpr GLenum LESS = 0x0201;
constexpr GLenum LEQUAL = 0x0203;
constexpr GLenum ALWAYS = 0x0207;
constexpr GLenum EQUAL = 0x0202;
constexpr GLenum KEEP = 0x1E00;
constexpr GLenum REPLACE = 0x1E01;
constexpr GLenum INCR = 0x1E02;
constexpr GLenum BACK = 0x0405;
constexpr GLenum FRONT = 0x0404;
constexpr GLenum CCW = 0x0901;
constexpr GLenum CW = 0x0900;
constexpr GLenum UNSIGNED_BYTE = 0x1401;
constexpr GLenum UNSIGNED_SHORT = 0x1403;
constexpr GLenum UNSIGNED_INT = 0x1405;
constexpr GLenum HALF_FLOAT = 0x140B;
constexpr GLenum FLOAT = 0x1406;
constexpr GLenum RGBA = 0x1908;
constexpr GLenum RGBA_INTEGER = 0x8D99;
constexpr GLenum RED = 0x1903;
constexpr GLenum DEPTH_COMPONENT = 0x1902;
constexpr GLenum STENCIL_INDEX = 0x1901;
constexpr GLenum RGBA8 = 0x8058;
constexpr GLenum R8 = 0x8229;
constexpr GLenum RG8 = 0x822B;
constexpr GLenum R16 = 0x822A;
constexpr GLenum R16F = 0x822D;
constexpr GLenum RG16F = 0x822F;
constexpr GLenum RG32F = 0x8230;
constexpr GLenum R16UI = 0x8234;
constexpr GLenum RGBA16 = 0x805B;
constexpr GLenum RGBA8UI = 0x8D7C;
constexpr GLenum RGBA16UI = 0x8D76;
constexpr GLenum RG = 0x8227;
constexpr GLenum RED_INTEGER = 0x8D94;
constexpr GLenum RGBA16F = 0x881A;
constexpr GLenum RGBA32F = 0x8814;
constexpr GLenum R32F = 0x822E;
constexpr GLenum RGBA32UI = 0x8D70;
constexpr GLenum DEPTH_COMPONENT32F = 0x8CAC;
constexpr GLenum DEPTH24_STENCIL8 = 0x88F0;
constexpr GLenum DEPTH32F_STENCIL8 = 0x8CAD;
constexpr GLenum TEXTURE_2D = 0x0DE1;
constexpr GLenum TEXTURE_2D_MULTISAMPLE = 0x9100;
constexpr GLenum TEXTURE0 = 0x84C0;
constexpr GLenum TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum TEXTURE_WRAP_S = 0x2802;
constexpr GLenum TEXTURE_WRAP_T = 0x2803;
constexpr GLenum TEXTURE_BASE_LEVEL = 0x813C;
constexpr GLenum TEXTURE_MAX_LEVEL = 0x813D;
constexpr GLenum NEAREST = 0x2600;
constexpr GLenum LINEAR = 0x2601;
constexpr GLenum LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum REPEAT = 0x2901;
constexpr GLenum CLAMP_TO_EDGE = 0x812F;
constexpr GLenum ARRAY_BUFFER = 0x8892;
constexpr GLenum ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum STATIC_DRAW = 0x88E4;
constexpr GLenum DYNAMIC_DRAW = 0x88E8;
constexpr GLenum MAP_WRITE_BIT = 0x0002;
constexpr GLenum MAP_INVALIDATE_RANGE_BIT = 0x0004;
constexpr GLenum MAP_FLUSH_EXPLICIT_BIT = 0x0010;
constexpr GLenum FRAGMENT_SHADER = 0x8B30;
constexpr GLenum VERTEX_SHADER = 0x8B31;
constexpr GLenum COMPILE_STATUS = 0x8B81;
constexpr GLenum LINK_STATUS = 0x8B82;
constexpr GLenum INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum FRAMEBUFFER = 0x8D40;
constexpr GLenum READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum RENDERBUFFER = 0x8D41;
constexpr GLenum COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum DEPTH_STENCIL_ATTACHMENT = 0x821A;
constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum VENDOR = 0x1F00;
constexpr GLenum RENDERER = 0x1F01;
constexpr GLenum VERSION = 0x1F02;
constexpr GLenum EXTENSIONS = 0x1F03;
constexpr GLenum NUM_EXTENSIONS = 0x821D;
constexpr GLenum MAJOR_VERSION = 0x821B;
constexpr GLenum MINOR_VERSION = 0x821C;
constexpr GLenum MAX_COLOR_ATTACHMENTS = 0x8CDF;
constexpr GLenum MAX_DRAW_BUFFERS = 0x8824;
constexpr GLenum MAX_SAMPLES = 0x8D57;
constexpr GLenum PACK_ALIGNMENT = 0x0D05;
constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum SAMPLES = 0x80A9;
constexpr GLenum NO_ERROR_ = 0;
constexpr GLenum DEVICE_UUID_EXT = 0x9597;
constexpr GLenum SCISSOR_BOX = 0x0C10;
}  // namespace glc
}  // namespace lsw::render::gl
