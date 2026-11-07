#include "sanity/gl/format_checks.hpp"

#include "sanity/gl/kit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::gl {

namespace {

struct Format {
    int index = 0;
    PIXELFORMATDESCRIPTOR pfd{};

    [[nodiscard]] bool generic() const {
        return (pfd.dwFlags & PFD_GENERIC_FORMAT) != 0 && (pfd.dwFlags & PFD_GENERIC_ACCELERATED) == 0;
    }

    [[nodiscard]] bool window_gl() const {
        return (pfd.dwFlags & PFD_SUPPORT_OPENGL) != 0 && (pfd.dwFlags & PFD_DRAW_TO_WINDOW) != 0;
    }

    [[nodiscard]] bool rgba() const { return pfd.iPixelType == PFD_TYPE_RGBA; }
};

bool describe_all(HDC dc, std::vector<Format>& out, std::string& error) {
    const int count = ::DescribePixelFormat(dc, 1, sizeof(PIXELFORMATDESCRIPTOR), nullptr);
    if (count <= 0) {
        error = "DescribePixelFormat(NULL) returned " + std::to_string(count) + " (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    for (int i = 1; i <= count; ++i) {
        Format format;
        format.index = i;
        if (::DescribePixelFormat(dc, i, sizeof(format.pfd), &format.pfd) == 0) {
            error = "DescribePixelFormat(" + std::to_string(i) + ") failed (error " + std::to_string(::GetLastError()) + ")";
            return false;
        }
        out.push_back(format);
    }
    return true;
}

std::string label(const Format& format) {
    return "format " + std::to_string(format.index) + " (color " + std::to_string(format.pfd.cColorBits) + ", alpha " +
           std::to_string(format.pfd.cAlphaBits) + ", depth " + std::to_string(format.pfd.cDepthBits) + ", stencil " +
           std::to_string(format.pfd.cStencilBits) + ", flags 0x" + Text::hex(format.pfd.dwFlags, 8) + ")";
}

bool fresh_window(GlRig& rig, TestWindow& window, std::string& error) {
    return window.create(rig.window_config(64, 64, false), error);
}

int attribute(GlRig& rig, int index, int name, bool& ok) {
    int value = 0;
    ok = rig.wgl.get_pixel_format_attribs != nullptr && rig.wgl.get_pixel_format_attribs(rig.dc(), index, 0, 1, &name, &value) != FALSE;
    return value;
}

}  // namespace

Outcome FormatChecks::enumerate_choosable(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    std::vector<Format> formats;
    if (!describe_all(rig.dc(), formats, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int settable = 0;
    int generic = 0;
    for (const Format& format : formats) {
        const PIXELFORMATDESCRIPTOR& pfd = format.pfd;
        if (pfd.nSize != sizeof(pfd) || pfd.nVersion != 1) {
            problems.add(label(format) + ": nSize " + std::to_string(pfd.nSize) + " nVersion " + std::to_string(pfd.nVersion));
        }
        PIXELFORMATDESCRIPTOR again{};
        if (::DescribePixelFormat(rig.dc(), format.index, sizeof(again), &again) == 0 || std::memcmp(&pfd, &again, sizeof(pfd)) != 0) {
            problems.add(label(format) + ": a second DescribePixelFormat returned different data");
        }
        if (format.rgba()) {
            if (pfd.cRedBits + pfd.cGreenBits + pfd.cBlueBits > pfd.cColorBits || pfd.cColorBits > 64) {
                problems.add(label(format) + ": red+green+blue bits exceed cColorBits");
            }
            if (pfd.cAlphaBits > 16) {
                problems.add(label(format) + ": cAlphaBits " + std::to_string(pfd.cAlphaBits));
            }
        }
        if (pfd.cDepthBits > 32 || pfd.cStencilBits > 16) {
            problems.add(label(format) + ": depth or stencil bits out of range");
        }
        if (!format.window_gl()) {
            continue;
        }
        generic += format.generic() ? 1 : 0;
        TestWindow window;
        if (!fresh_window(rig, window, error)) {
            return Outcome::fail(error);
        }
        if (::SetPixelFormat(window.dc(), format.index, const_cast<PIXELFORMATDESCRIPTOR*>(&pfd)) == FALSE) {
            problems.add(label(format) + ": SetPixelFormat on a fresh window failed (error " + std::to_string(::GetLastError()) + ")");
        } else if (::GetPixelFormat(window.dc()) != format.index) {
            problems.add(label(format) + ": GetPixelFormat returned " + std::to_string(::GetPixelFormat(window.dc())));
        } else {
            ++settable;
        }
    }
    context.measure("formats", static_cast<long long>(formats.size()));
    context.measure("formats_settable", settable);
    context.measure("formats_generic", generic);
    if (settable == 0 && problems.empty()) {
        problems.add("no format has PFD_SUPPORT_OPENGL and PFD_DRAW_TO_WINDOW");
    }
    return problems.outcome();
}

Outcome FormatChecks::every_format_renders(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    std::vector<Format> formats;
    if (!describe_all(rig.dc(), formats, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int rendered = 0;
    int skipped_generic = 0;
    for (const Format& format : formats) {
        if (!format.window_gl() || !format.rgba()) {
            continue;
        }
        if (format.generic()) {
            ++skipped_generic;
            continue;
        }
        TestWindow window;
        if (!fresh_window(rig, window, error)) {
            return Outcome::fail(error);
        }
        PIXELFORMATDESCRIPTOR pfd = format.pfd;
        if (::SetPixelFormat(window.dc(), format.index, &pfd) == FALSE) {
            problems.add(label(format) + ": SetPixelFormat failed");
            continue;
        }
        GlLibrary& library = GlLibrary::instance();
        HGLRC gl_context = library.create_context(window.dc());
        if (gl_context == nullptr || library.make_current(window.dc(), gl_context) == FALSE) {
            problems.add(label(format) + ": no context can be created or made current (error " + std::to_string(::GetLastError()) + ")");
            if (gl_context != nullptr) {
                library.delete_context(gl_context);
            }
            continue;
        }
        GlApi api;
        if (!api.load(library, error)) {
            problems.add(label(format) + ": " + error);
        } else {
            const int w = window.width();
            const int h = window.height();
            const float clear[3] = {0.25f, 0.5f, 0.75f};
            api.glViewport(0, 0, w, h);
            api.glClearColor(clear[0], clear[1], clear[2], 1.0f);
            api.glClear(glc::COLOR_BUFFER_BIT);
            api.glBegin(glc::TRIANGLES);
            api.glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
            api.glVertex2f(-1.0f, -1.0f);
            api.glVertex2f(1.0f, -1.0f);
            api.glVertex2f(-1.0f, 1.0f);
            api.glEnd();
            api.glFinish();
            std::uint8_t inside[4] = {};
            std::uint8_t outside[4] = {};
            api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
            api.glReadPixels(w / 4, h / 4, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, inside);
            api.glReadPixels(w * 3 / 4, h * 3 / 4, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, outside);
            const unsigned failure = api.drain_errors();
            if (failure != 0) {
                problems.add(label(format) + ": " + GlApi::error_name(failure));
            }
            auto tolerance = [](int bits) { return bits >= 8 ? 1 : static_cast<int>(std::ceil(255.0 / (std::pow(2.0, bits) - 1.0) / 2.0)) + 1; };
            auto expect = [&](const std::uint8_t* got, const float* want, const char* where) {
                const int bits[3] = {format.pfd.cRedBits, format.pfd.cGreenBits, format.pfd.cBlueBits};
                for (int c = 0; c < 3; ++c) {
                    const int expected = static_cast<int>(std::lround(want[c] * 255.0f));
                    if (bits[c] > 0 && std::abs(static_cast<int>(got[c]) - expected) > tolerance(bits[c])) {
                        problems.add(label(format) + ": " + where + " channel " + std::to_string(c) + " is " + std::to_string(got[c]) +
                                     ", expected " + std::to_string(expected) + " within " + std::to_string(tolerance(bits[c])));
                        break;
                    }
                }
            };
            const float red[3] = {1.0f, 0.0f, 0.0f};
            expect(inside, red, "inside the triangle");
            expect(outside, clear, "outside the triangle");
            if ((format.pfd.dwFlags & PFD_DOUBLEBUFFER) != 0 && ::SwapBuffers(window.dc()) == FALSE) {
                problems.add(label(format) + ": SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")");
            }
            ++rendered;
        }
        library.make_current(nullptr, nullptr);
        library.delete_context(gl_context);
    }
    context.measure("formats_rendered", rendered);
    context.measure("formats_generic_skipped", skipped_generic);
    if (rendered == 0 && problems.empty()) {
        problems.add("no non-generic RGBA window format exists to render with");
    }
    return problems.outcome();
}

Outcome FormatChecks::choose_matches_request(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    std::vector<Format> formats;
    if (!describe_all(rig.dc(), formats, error)) {
        return Outcome::fail(error);
    }
    struct Request {
        int color;
        int depth;
        int stencil;
        bool double_buffer;
    };
    const Request requests[] = {{32, 24, 8, true}, {24, 16, 0, true}, {32, 0, 0, true}, {16, 16, 0, false}, {24, 24, 8, false}, {32, 32, 0, true}};
    Problems problems;
    for (const Request& request : requests) {
        const std::string text = "ChoosePixelFormat(color " + std::to_string(request.color) + ", depth " + std::to_string(request.depth) +
                                 ", stencil " + std::to_string(request.stencil) + (request.double_buffer ? ", double" : ", single") + ")";
        PIXELFORMATDESCRIPTOR want{};
        want.nSize = sizeof(want);
        want.nVersion = 1;
        want.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | (request.double_buffer ? PFD_DOUBLEBUFFER : 0);
        want.iPixelType = PFD_TYPE_RGBA;
        want.cColorBits = static_cast<BYTE>(request.color);
        want.cDepthBits = static_cast<BYTE>(request.depth);
        want.cStencilBits = static_cast<BYTE>(request.stencil);
        const bool possible = std::any_of(formats.begin(), formats.end(), [&](const Format& f) {
            return f.window_gl() && f.rgba() && f.pfd.cColorBits >= request.color && f.pfd.cDepthBits >= request.depth &&
                   f.pfd.cStencilBits >= request.stencil && ((f.pfd.dwFlags & PFD_DOUBLEBUFFER) != 0) == request.double_buffer;
        });
        const int chosen = ::ChoosePixelFormat(rig.dc(), &want);
        if (chosen <= 0 || chosen > static_cast<int>(formats.size())) {
            problems.add(text + " returned " + std::to_string(chosen) + " (error " + std::to_string(::GetLastError()) + ")");
            continue;
        }
        const Format& got = formats[static_cast<std::size_t>(chosen - 1)];
        if (!got.window_gl()) {
            problems.add(text + " chose " + label(got) + " which lacks PFD_SUPPORT_OPENGL or PFD_DRAW_TO_WINDOW");
        }
        if (possible && (got.pfd.cColorBits < request.color || got.pfd.cDepthBits < request.depth || got.pfd.cStencilBits < request.stencil ||
                         ((got.pfd.dwFlags & PFD_DOUBLEBUFFER) != 0) != request.double_buffer)) {
            problems.add(text + " chose " + label(got) + " although a satisfying format exists");
        }
    }
    if (rig.wgl.choose_pixel_format == nullptr || rig.wgl.get_pixel_format_attribs == nullptr) {
        context.note("wglChoosePixelFormatARB is not exposed; only ChoosePixelFormat was checked");
        return problems.outcome();
    }
    using namespace wglc;
    struct Criteria {
        std::vector<int> pairs;
        std::vector<int> minimum;
        const char* name;
    };
    const Criteria criteria[] = {
        {{DRAW_TO_WINDOW, 1, SUPPORT_OPENGL, 1, DOUBLE_BUFFER, 1, PIXEL_TYPE, TYPE_RGBA, COLOR_BITS, 24, DEPTH_BITS, 24, STENCIL_BITS, 8},
         {COLOR_BITS, 24, DEPTH_BITS, 24, STENCIL_BITS, 8},
         "double RGBA 24/24/8"},
        {{DRAW_TO_WINDOW, 1, SUPPORT_OPENGL, 1, DOUBLE_BUFFER, 0, PIXEL_TYPE, TYPE_RGBA, COLOR_BITS, 16},
         {COLOR_BITS, 16},
         "single RGBA 16"},
        {{DRAW_TO_WINDOW, 1, SUPPORT_OPENGL, 1, PIXEL_TYPE, TYPE_RGBA, ALPHA_BITS, 8, DEPTH_BITS, 16},
         {ALPHA_BITS, 8, DEPTH_BITS, 16},
         "RGBA with alpha 8 and depth 16"},
        {{DRAW_TO_WINDOW, 1, SUPPORT_OPENGL, 1, PIXEL_TYPE, TYPE_RGBA, DEPTH_BITS, 64}, {DEPTH_BITS, 64}, "depth 64 (unsatisfiable)"},
    };
    const int all_count = ::DescribePixelFormat(rig.dc(), 1, sizeof(PIXELFORMATDESCRIPTOR), nullptr);
    for (const Criteria& criterion : criteria) {
        std::vector<int> attributes = criterion.pairs;
        attributes.push_back(0);
        int found[256] = {};
        UINT count = 0;
        const BOOL ok = rig.wgl.choose_pixel_format(rig.dc(), attributes.data(), nullptr, 256, found, &count);
        const std::string text = std::string("wglChoosePixelFormatARB(") + criterion.name + ")";
        if (ok == FALSE) {
            count = 0;
        }
        int expected_count = 0;
        for (int i = 1; i <= all_count; ++i) {
            bool satisfied = true;
            for (std::size_t p = 0; p + 1 < criterion.pairs.size(); p += 2) {
                bool known = false;
                const int value = attribute(rig, i, criterion.pairs[p], known);
                const bool minimum_attr = std::find(criterion.minimum.begin(), criterion.minimum.end(), criterion.pairs[p]) != criterion.minimum.end();
                if (!known || (minimum_attr ? value < criterion.pairs[p + 1] : value != criterion.pairs[p + 1])) {
                    satisfied = false;
                }
            }
            expected_count += satisfied ? 1 : 0;
        }
        if (static_cast<int>(count) != std::min(expected_count, 256)) {
            problems.add(text + " returned " + std::to_string(count) + " formats, the attribute queries find " + std::to_string(expected_count) + " that satisfy it");
            continue;
        }
        for (UINT n = 0; n < count; ++n) {
            for (std::size_t p = 0; p + 1 < criterion.pairs.size(); p += 2) {
                bool known = false;
                const int value = attribute(rig, found[n], criterion.pairs[p], known);
                const bool minimum_attr = std::find(criterion.minimum.begin(), criterion.minimum.end(), criterion.pairs[p]) != criterion.minimum.end();
                if (!known || (minimum_attr ? value < criterion.pairs[p + 1] : value != criterion.pairs[p + 1])) {
                    problems.add(text + " returned format " + std::to_string(found[n]) + " whose attribute 0x" + Text::hex(static_cast<unsigned>(criterion.pairs[p]), 4) +
                                 " is " + std::to_string(value) + ", request " + std::to_string(criterion.pairs[p + 1]));
                    break;
                }
            }
        }
    }
    return problems.outcome();
}

Outcome FormatChecks::arb_attribs_agree_with_describe(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    if (rig.wgl.get_pixel_format_attribs == nullptr) {
        return Kit::not_advertised("WGL_ARB_pixel_format");
    }
    std::vector<Format> formats;
    if (!describe_all(rig.dc(), formats, error)) {
        return Outcome::fail(error);
    }
    using namespace wglc;
    const int names[] = {NUMBER_PIXEL_FORMATS, DRAW_TO_WINDOW, SUPPORT_OPENGL, DOUBLE_BUFFER, PIXEL_TYPE, COLOR_BITS, RED_BITS, GREEN_BITS,
                         BLUE_BITS, ALPHA_BITS, DEPTH_BITS, STENCIL_BITS, SWAP_METHOD};
    Problems problems;
    int icd_formats = 0;
    for (const Format& format : formats) {
        if ((format.pfd.dwFlags & PFD_GENERIC_FORMAT) == 0) {
            ++icd_formats;
        }
    }
    for (const Format& format : formats) {
        if ((format.pfd.dwFlags & PFD_GENERIC_FORMAT) != 0) {
            continue;
        }
        int values[13] = {};
        if (rig.wgl.get_pixel_format_attribs(rig.dc(), format.index, 0, 13, names, values) == FALSE) {
            problems.add(label(format) + ": wglGetPixelFormatAttribivARB failed (error " + std::to_string(::GetLastError()) + ")");
            continue;
        }
        const PIXELFORMATDESCRIPTOR& pfd = format.pfd;
        auto differ = [&](const char* what, int arb, int describe) {
            if (arb != describe) {
                problems.add(label(format) + ": " + what + " is " + std::to_string(arb) + " through the ARB query and " + std::to_string(describe) + " through DescribePixelFormat");
            }
        };
        if (values[0] < icd_formats) {
            problems.add(label(format) + ": NUMBER_PIXEL_FORMATS " + std::to_string(values[0]) + " is below the count of non-generic formats " + std::to_string(icd_formats));
        }
        differ("DRAW_TO_WINDOW", values[1] != 0, (pfd.dwFlags & PFD_DRAW_TO_WINDOW) != 0);
        differ("SUPPORT_OPENGL", values[2] != 0, (pfd.dwFlags & PFD_SUPPORT_OPENGL) != 0);
        differ("DOUBLE_BUFFER", values[3] != 0, (pfd.dwFlags & PFD_DOUBLEBUFFER) != 0);
        if (format.rgba()) {
            differ("PIXEL_TYPE is RGBA", values[4] == TYPE_RGBA, 1);
            differ("COLOR_BITS", values[5], pfd.cColorBits);
            differ("RED_BITS", values[6], pfd.cRedBits);
            differ("GREEN_BITS", values[7], pfd.cGreenBits);
            differ("BLUE_BITS", values[8], pfd.cBlueBits);
            differ("ALPHA_BITS", values[9], pfd.cAlphaBits);
        }
        differ("DEPTH_BITS", values[10], pfd.cDepthBits);
        differ("STENCIL_BITS", values[11], pfd.cStencilBits);
        const int method = (pfd.dwFlags & PFD_SWAP_EXCHANGE) != 0 ? SWAP_EXCHANGE : (pfd.dwFlags & PFD_SWAP_COPY) != 0 ? SWAP_COPY : SWAP_UNDEFINED;
        if ((pfd.dwFlags & PFD_DOUBLEBUFFER) != 0) {
            differ("SWAP_METHOD", values[12], method);
        }
    }
    context.measure("formats", static_cast<long long>(formats.size()));
    return problems.outcome();
}

Outcome FormatChecks::set_twice_fails(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    std::vector<Format> formats;
    if (!describe_all(rig.dc(), formats, error)) {
        return Outcome::fail(error);
    }
    std::vector<const Format*> choosable;
    for (const Format& format : formats) {
        if (format.window_gl() && !format.generic()) {
            choosable.push_back(&format);
        }
    }
    if (choosable.size() < 2) {
        return Context::skip("fewer than two window formats are choosable, so a different second format cannot be requested");
    }
    Problems problems;
    const std::size_t pairs[][2] = {{0, 1}, {choosable.size() - 1, 0}};
    for (const auto& pair : pairs) {
        TestWindow window;
        if (!fresh_window(rig, window, error)) {
            return Outcome::fail(error);
        }
        PIXELFORMATDESCRIPTOR first = choosable[pair[0]]->pfd;
        PIXELFORMATDESCRIPTOR second = choosable[pair[1]]->pfd;
        if (::SetPixelFormat(window.dc(), choosable[pair[0]]->index, &first) == FALSE) {
            problems.add("the first SetPixelFormat(" + std::to_string(choosable[pair[0]]->index) + ") failed");
            continue;
        }
        if (::SetPixelFormat(window.dc(), choosable[pair[1]]->index, &second) != FALSE) {
            problems.add("SetPixelFormat(" + std::to_string(choosable[pair[1]]->index) + ") succeeded on a window that already has format " +
                         std::to_string(choosable[pair[0]]->index));
        }
        if (::GetPixelFormat(window.dc()) != choosable[pair[0]]->index) {
            problems.add("after the failed second SetPixelFormat the window reports format " + std::to_string(::GetPixelFormat(window.dc())) +
                         ", expected " + std::to_string(choosable[pair[0]]->index));
        }
    }
    return problems.outcome();
}

Outcome FormatChecks::srgb_and_float(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error)) {
        return Outcome::fail(error);
    }
    using namespace wglc;
    const std::string list = rig.wgl.extensions(rig.dc());
    const bool srgb_advertised = WglApi::contains(list, "WGL_ARB_framebuffer_sRGB") || WglApi::contains(list, "WGL_EXT_framebuffer_sRGB");
    const bool float_advertised = WglApi::contains(list, "WGL_ARB_pixel_format_float") || WglApi::contains(list, "WGL_ATI_pixel_format_float");
    if (rig.wgl.get_pixel_format_attribs == nullptr) {
        return Kit::not_advertised("WGL_ARB_pixel_format");
    }
    bool counted = false;
    const int total = attribute(rig, 1, NUMBER_PIXEL_FORMATS, counted);
    if (!counted || total <= 0) {
        return Outcome::fail("WGL_NUMBER_PIXEL_FORMATS_ARB cannot be queried");
    }
    Problems problems;
    int srgb_format = 0;
    int float_format = 0;
    bool any_float = false;
    for (int index = 1; index <= total; ++index) {
        bool known = false;
        const int srgb = attribute(rig, index, FRAMEBUFFER_SRGB_CAPABLE, known);
        const bool srgb_known = known;
        const int type = attribute(rig, index, PIXEL_TYPE, known);
        const bool is_srgb = srgb_known && srgb != 0;
        const bool is_float = known && type == TYPE_RGBA_FLOAT;
        bool window_ok = false;
        const int draw = attribute(rig, index, DRAW_TO_WINDOW, window_ok);
        bool accel_ok = false;
        const int acceleration = attribute(rig, index, ACCELERATION, accel_ok);
        const bool hardware = !accel_ok || acceleration == FULL_ACCELERATION;
        const std::string name = "format " + std::to_string(index);
        if (is_srgb && !srgb_advertised) {
            problems.add(name + " reports an sRGB-capable framebuffer but neither WGL_ARB_framebuffer_sRGB nor WGL_EXT_framebuffer_sRGB is advertised");
        }
        if (is_float && !float_advertised) {
            problems.add(name + " reports a floating-point pixel type but WGL_ARB_pixel_format_float is not advertised");
        }
        if (is_srgb && draw != 0 && srgb_format == 0 && hardware) {
            srgb_format = index;
        }
        if (is_float) {
            any_float = true;
            if (draw != 0 && float_format == 0 && hardware) {
                float_format = index;
            }
        }
    }
    context.measure("srgb_advertised", srgb_advertised);
    context.measure("float_advertised", float_advertised);
    if (srgb_advertised && srgb_format == 0) {
        problems.add("WGL_ARB_framebuffer_sRGB is advertised but no window format is sRGB-capable");
    }
    if (float_advertised && !any_float) {
        problems.add("WGL_ARB_pixel_format_float is advertised but no format has a floating-point pixel type");
    }
    if (float_advertised && any_float && float_format == 0) {
        context.note("floating-point formats exist but none draws to a window, so only their presence was checked");
    }
    auto with_format = [&](int index, const char* what, auto&& body) {
        TestWindow window;
        if (!fresh_window(rig, window, error)) {
            problems.add(error);
            return;
        }
        PIXELFORMATDESCRIPTOR pfd{};
        ::DescribePixelFormat(window.dc(), index, sizeof(pfd), &pfd);
        if (::SetPixelFormat(window.dc(), index, &pfd) == FALSE) {
            problems.add(std::string(what) + " format " + std::to_string(index) + ": SetPixelFormat failed");
            return;
        }
        GlLibrary& library = GlLibrary::instance();
        HGLRC gl_context = library.create_context(window.dc());
        if (gl_context == nullptr || library.make_current(window.dc(), gl_context) == FALSE) {
            problems.add(std::string(what) + " format " + std::to_string(index) + ": no context (error " + std::to_string(::GetLastError()) + ")");
            return;
        }
        GlApi api;
        if (api.load(library, error)) {
            body(api, window);
        } else {
            problems.add(error);
        }
        library.make_current(nullptr, nullptr);
        library.delete_context(gl_context);
    };
    if (srgb_format != 0) {
        with_format(srgb_format, "sRGB", [&](GlApi& api, TestWindow& window) {
            const int w = window.width();
            const int h = window.height();
            auto draw_blend = [&](bool encode) {
                if (encode) {
                    api.glEnable(glc::FRAMEBUFFER_SRGB);
                } else {
                    api.glDisable(glc::FRAMEBUFFER_SRGB);
                }
                api.glViewport(0, 0, w, h);
                api.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                api.glClear(glc::COLOR_BUFFER_BIT);
                api.glEnable(glc::BLEND);
                api.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
                api.glBegin(glc::TRIANGLES);
                api.glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
                api.glVertex2f(-1.0f, -1.0f);
                api.glVertex2f(3.0f, -1.0f);
                api.glVertex2f(-1.0f, 3.0f);
                api.glEnd();
                api.glDisable(glc::BLEND);
                api.glFinish();
                std::uint8_t pixel[4] = {};
                api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
                api.glReadPixels(w / 2, h / 2, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
                return static_cast<int>(pixel[0]);
            };
            const int encoded = draw_blend(true);
            const int plain = draw_blend(false);
            const unsigned failure = api.drain_errors();
            if (failure != 0) {
                problems.add("sRGB rendering gave " + GlApi::error_name(failure));
            }
            context.measure("srgb.blend_encoded", encoded);
            context.measure("srgb.blend_plain", plain);
            if (std::abs(encoded - 188) > 2) {
                problems.add("a 0.5 alpha blend of white over black with GL_FRAMEBUFFER_SRGB enabled reads " + std::to_string(encoded) +
                             ", the sRGB encoding of linear 0.5 is 188");
            }
            if (std::abs(plain - 128) > 1) {
                problems.add("the same blend with GL_FRAMEBUFFER_SRGB disabled reads " + std::to_string(plain) + ", expected 128");
            }
        });
    }
    if (float_format != 0) {
        with_format(float_format, "float", [&](GlApi& api, TestWindow& window) {
            const int w = window.width();
            const int h = window.height();
            if (api.glClampColor == nullptr) {
                problems.add("a floating-point format exists but glClampColor does not resolve");
                return;
            }
            api.glClampColor(0x891A, 0);
            api.glClampColor(0x891B, 0);
            api.glClampColor(glc::CLAMP_READ_COLOR, 0);
            api.glViewport(0, 0, w, h);
            api.glClearColor(2.5f, -1.0f, 0.25f, 1.0f);
            api.glClear(glc::COLOR_BUFFER_BIT);
            float cleared[4] = {};
            api.glReadPixels(w / 2, h / 2, 1, 1, glc::RGBA, glc::FLOAT, cleared);
            api.glBegin(glc::TRIANGLES);
            api.glColor4f(4.0f, 0.5f, 1.5f, 1.0f);
            api.glVertex2f(-1.0f, -1.0f);
            api.glVertex2f(3.0f, -1.0f);
            api.glVertex2f(-1.0f, 3.0f);
            api.glEnd();
            float drawn[4] = {};
            api.glReadPixels(w / 2, h / 2, 1, 1, glc::RGBA, glc::FLOAT, drawn);
            const unsigned failure = api.drain_errors();
            if (failure != 0) {
                problems.add("floating-point rendering gave " + GlApi::error_name(failure));
            }
            if (cleared[0] != 2.5f || cleared[1] != -1.0f || cleared[2] != 0.25f) {
                problems.add("clearing a floating-point window format to (2.5,-1,0.25) reads (" + std::to_string(cleared[0]) + "," +
                             std::to_string(cleared[1]) + "," + std::to_string(cleared[2]) + ")");
            }
            if (drawn[0] != 4.0f || drawn[1] != 0.5f || drawn[2] != 1.5f) {
                problems.add("drawing (4,0.5,1.5) into a floating-point window format reads (" + std::to_string(drawn[0]) + "," +
                             std::to_string(drawn[1]) + "," + std::to_string(drawn[2]) + ")");
            }
        });
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
