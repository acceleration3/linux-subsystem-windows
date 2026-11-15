#pragma once

namespace lsw::suites::wglc {
constexpr int NUMBER_PIXEL_FORMATS = 0x2000;
constexpr int DRAW_TO_WINDOW = 0x2001;
constexpr int ACCELERATION = 0x2003;
constexpr int SWAP_METHOD = 0x2007;
constexpr int SUPPORT_OPENGL = 0x2010;
constexpr int DOUBLE_BUFFER = 0x2011;
constexpr int PIXEL_TYPE = 0x2013;
constexpr int COLOR_BITS = 0x2014;
constexpr int RED_BITS = 0x2015;
constexpr int GREEN_BITS = 0x2017;
constexpr int BLUE_BITS = 0x2019;
constexpr int ALPHA_BITS = 0x201B;
constexpr int DEPTH_BITS = 0x2022;
constexpr int STENCIL_BITS = 0x2023;
constexpr int SAMPLE_BUFFERS = 0x2041;
constexpr int SAMPLES = 0x2042;
constexpr int NO_ACCELERATION = 0x2025;
constexpr int GENERIC_ACCELERATION = 0x2026;
constexpr int FULL_ACCELERATION = 0x2027;
constexpr int SWAP_EXCHANGE = 0x2028;
constexpr int SWAP_COPY = 0x2029;
constexpr int SWAP_UNDEFINED = 0x202A;
constexpr int TYPE_RGBA = 0x202B;
constexpr int TYPE_RGBA_FLOAT = 0x21A0;
constexpr int FRAMEBUFFER_SRGB_CAPABLE = 0x20A9;
constexpr int VERSION_ERROR = 0x2095;
constexpr int PROFILE_ERROR = 0x2096;
constexpr int CONTEXT_DEBUG_BIT = 0x1;
constexpr int CONTEXT_FORWARD_COMPATIBLE_BIT = 0x2;
constexpr int CONTEXT_ROBUST_ACCESS_BIT = 0x4;
constexpr int CONTEXT_CORE_PROFILE_BIT = 0x1;
constexpr int CONTEXT_COMPATIBILITY_PROFILE_BIT = 0x2;
constexpr int NO_RESET_NOTIFICATION = 0x8261;
constexpr int LOSE_CONTEXT_ON_RESET = 0x8252;
}  // namespace lsw::suites::wglc
