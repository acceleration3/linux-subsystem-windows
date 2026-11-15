#include "sanity/present/content_checks.hpp"

#include "core/text.hpp"
#include "sanity/interop/kit.hpp"
#include "sanity/present/chain_probe.hpp"
#include "sanity/present/kit.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/swap_rig.hpp"

#include <algorithm>
#include <chrono>
#include <memory>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kDrawnFrame = 21;

struct FormatCase {
    const char* name;
    DXGI_FORMAT chain;
    DXGI_FORMAT view;
    DXGI_SWAP_EFFECT effect;
    UINT buffers;
    bool linear;
    int tolerance;
};

constexpr FormatCase kFormatCases[] = {
    {"R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, false, 0},
    {"B8G8R8A8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, false, 0},
    {"R8G8B8A8_UNORM_SRGB", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_EFFECT_SEQUENTIAL, 1, true, 1},
    {"B8G8R8A8_UNORM through an sRGB view", DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, true, 1},
    {"R10G10B10A2_UNORM", DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, false, 1},
    {"R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, true, 3},
};

struct SizeCase {
    const char* name;
    int chain_width;
    int chain_height;
    int window_width;
    int window_height;
};

constexpr SizeCase kSizeCases[] = {
    {"odd 131x77", 131, 77, 131, 77},
    {"1x1", 1, 1, 160, 120},
    {"4K", 3840, 2160, 160, 120},
};

bool resize_window(TestWindow& window, int width, int height, std::string& error) {
    window.resize_client(width, height);
    if (window.width() != width || window.height() != height) {
        error = "the window client is " + std::to_string(window.width()) + "x" + std::to_string(window.height()) + " after asking for " + std::to_string(width) + "x" + std::to_string(height);
        return false;
    }
    return true;
}

bool fresh_window(SwapRig& rig, int width, int height, std::string& error) {
    rig.window.destroy();
    TestWindowConfig config;
    config.x = rig.options.x;
    config.y = rig.options.y;
    config.popup = true;
    config.width = width;
    config.height = height;
    config.visible = rig.options.visible;
    if (!rig.window.create(config, error)) {
        return false;
    }
    ::SetWindowPos(rig.window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return resize_window(rig.window, width, height, error);
}

void drain(SwapRig& rig) {
    std::string error;
    rig.device.context->ClearState();
    rig.device.context->Flush();
    static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
}

bool draw_quadrants(SwapRig& rig, D3d11FrameDraw& draw, DXGI_FORMAT view_format, int width, int height, int frame, bool linear, bool uniform, std::string& error) {
    Com<ID3D11Texture2D> buffer = rig.back_buffer(0);
    if (!buffer) {
        error = "no back buffer 0";
        return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = view_format;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    Com<ID3D11RenderTargetView> view;
    if (FAILED(rig.device.device->CreateRenderTargetView(buffer.get(), view_format == DXGI_FORMAT_UNKNOWN ? nullptr : &desc, view.put()))) {
        error = "CreateRenderTargetView over the back buffer failed";
        return false;
    }
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const std::uint32_t c = FrameImage::color(frame, uniform ? 0 : quadrant);
        float rgba[4] = {static_cast<float>((c >> 16) & 255u) / 255.0f, static_cast<float>((c >> 8) & 255u) / 255.0f, static_cast<float>(c & 255u) / 255.0f, 1.0f};
        if (linear) {
            rgba[0] = ChainProbe::linear_of(static_cast<int>((c >> 16) & 255u));
            rgba[1] = ChainProbe::linear_of(static_cast<int>((c >> 8) & 255u));
            rgba[2] = ChainProbe::linear_of(static_cast<int>(c & 255u));
        }
        draw.fill_rgba(view.get(), FrameImage::quadrant(width, height, quadrant), rgba);
    }
    rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

}  // namespace

Outcome ContentChecks::exact_pixels(Context& context) {
    Problems problems;
    int exercised = 0;
    struct Size {
        int w;
        int h;
    };
    for (const std::string& id : PresentPath::ids()) {
        if (id == "vk_immediate" || id == "vk_mailbox") {
            continue;
        }
        for (const Size size : {Size{128, 128}, Size{131, 77}, Size{257, 129}}) {
            std::unique_ptr<PresentPath> path = PresentPath::make(id);
            std::string error;
            const std::string tag = id + " " + std::to_string(size.w) + "x" + std::to_string(size.h);
            const Opened state = Kit::open(*path, size.w, size.h, error);
            if (state == Opened::Unsupported) {
                context.note(tag + " is not exercised: " + error);
                continue;
            }
            if (state == Opened::Failed) {
                problems.add(tag + ": " + error);
                continue;
            }
            ++exercised;
            for (const int frame : {7, 40}) {
                const PresentResult result = path->present(frame, 1);
                std::string detail;
                if (!result.ok) {
                    problems.add(tag + " frame " + std::to_string(frame) + ": " + result.error);
                } else if (!path->drain(error) || !Kit::settle(context, Kit::sanitize(tag) + ".frame" + std::to_string(frame), *path, FrameImage::render(frame, size.w, size.h), 0, detail)) {
                    problems.add(tag + " frame " + std::to_string(frame) + " differs from what was rendered: " + (detail.empty() ? error : detail));
                }
            }
            if (path->supports_image()) {
                for (int phase = 0; phase < 2; ++phase) {
                    const std::vector<std::uint32_t> image = FrameImage::gradient_checker(size.w, size.h, phase);
                    const PresentResult result = path->present_image(image, 1);
                    std::string detail;
                    if (!result.ok) {
                        problems.add(tag + " gradient " + std::to_string(phase) + ": " + result.error);
                    } else if (!path->drain(error) || !Kit::settle(context, Kit::sanitize(tag) + ".gradient" + std::to_string(phase), *path, image, 0, detail)) {
                        problems.add(tag + " gradient and checker pattern " + std::to_string(phase) + " differs from what was uploaded: " + (detail.empty() ? error : detail));
                    }
                }
            } else {
                context.note(tag + ": no pixel upload on this path, so only the rendered frames are compared");
            }
            path->close();
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened with the APIs enabled");
    }
    context.measure("paths_exercised", exercised);
    return problems.outcome();
}

Outcome ContentChecks::sizes_and_formats(Context& context) {
    SwapRig rig;
    SwapOptions options;
    options.width = 160;
    options.height = 120;
    options.popup = true;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    std::string error;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    D3d11FrameDraw draw;
    if (!draw.open(rig.device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int exercised = 0;
    for (const FormatCase& fc : kFormatCases) {
        UINT support = 0;
        const HRESULT queried = rig.device.device->CheckFormatSupport(fc.chain, &support);
        const bool displayable = SUCCEEDED(queried) && (support & D3D11_FORMAT_SUPPORT_DISPLAY) != 0 && (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
        for (const SizeCase& sc : kSizeCases) {
            const std::string tag = std::string(fc.name) + " " + sc.name;
            rig.chain.reset();
            drain(rig);
            if (!fresh_window(rig, sc.window_width, sc.window_height, error)) {
                problems.add(tag + ": " + error);
                continue;
            }
            SwapOptions chain_options = rig.options;
            chain_options.width = static_cast<UINT>(sc.chain_width);
            chain_options.height = static_cast<UINT>(sc.chain_height);
            chain_options.format = fc.chain;
            chain_options.effect = fc.effect;
            chain_options.buffers = fc.buffers;
            chain_options.scaling = sc.chain_width != sc.window_width ? static_cast<int>(DXGI_SCALING_STRETCH) : -1;
            const HRESULT created = rig.try_create(chain_options, rig.window.hwnd(), rig.chain);
            rig.options = chain_options;
            if (!displayable) {
                if (SUCCEEDED(created)) {
                    problems.add(tag + ": CheckFormatSupport reports no DISPLAY or RENDER_TARGET support (0x" + Text::hex(support, 8) + ") but the swap chain was created");
                }
                continue;
            }
            if (FAILED(created)) {
                problems.add(tag + ": CheckFormatSupport reports display support but CreateSwapChainForHwnd returned " + D3d11Tools::hr(created));
                continue;
            }
            ++exercised;
            const bool tiny = sc.chain_width == 1;
            if (!draw_quadrants(rig, draw, fc.view, sc.chain_width, sc.chain_height, kDrawnFrame, fc.linear, tiny, error)) {
                problems.add(tag + ": " + error);
                continue;
            }
            const HRESULT presented = rig.chain->Present(1, 0);
            if (FAILED(presented)) {
                problems.add(tag + ": Present returned " + D3d11Tools::hr(presented));
                continue;
            }
            drain(rig);
            const RECT client = ScreenCapture::client_on_screen(rig.window.hwnd());
            std::string detail;
            if (sc.chain_width == sc.window_width) {
                if (!interop::Kit::settle_screen(context, Kit::sanitize(tag), client, FrameImage::render(kDrawnFrame, sc.chain_width, sc.chain_height), fc.tolerance, 3000, detail)) {
                    problems.add(tag + ": the window differs from the rendered quadrants beyond " + std::to_string(fc.tolerance) + ": " + detail);
                }
            } else if (tiny) {
                const std::vector<std::uint32_t> uniform = interop::Kit::uniform(1, 1, FrameImage::color(kDrawnFrame, 0) & 0xFFFFFFu);
                const RECT centre{(client.left + client.right) / 2, (client.top + client.bottom) / 2, (client.left + client.right) / 2 + 1, (client.top + client.bottom) / 2 + 1};
                if (!interop::Kit::settle_screen(context, Kit::sanitize(tag), centre, uniform, fc.tolerance, 3000, detail)) {
                    problems.add(tag + ": the stretched pixel differs beyond " + std::to_string(fc.tolerance) + ": " + detail);
                }
            } else {
                const RECT whole{0, 0, client.right - client.left, client.bottom - client.top};
                if (!ChainProbe::poll_quadrants(client, whole, kDrawnFrame, fc.tolerance, 3000, detail)) {
                    problems.add(tag + ": " + detail);
                }
            }
        }
    }
    rig.chain.reset();
    drain(rig);
    draw.release();
    context.measure("cases_exercised", exercised);
    return problems.outcome();
}

Outcome ContentChecks::scaling_modes(Context& context) {
    SwapRig rig;
    SwapOptions options;
    options.width = 128;
    options.height = 96;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    std::string error;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    D3d11FrameDraw draw;
    if (!draw.open(rig.device, error)) {
        return Outcome::fail(error);
    }
    struct Case {
        const char* name;
        DXGI_SWAP_EFFECT effect;
        UINT buffers;
        DXGI_SCALING scaling;
        bool may_be_rejected;
    };
    const Case cases[] = {
        {"blt STRETCH", DXGI_SWAP_EFFECT_SEQUENTIAL, 1, DXGI_SCALING_STRETCH, false},
        {"blt NONE", DXGI_SWAP_EFFECT_SEQUENTIAL, 1, DXGI_SCALING_NONE, true},
        {"flip STRETCH", DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, DXGI_SCALING_STRETCH, false},
        {"flip NONE", DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, DXGI_SCALING_NONE, false},
        {"flip ASPECT_RATIO_STRETCH", DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, DXGI_SCALING_ASPECT_RATIO_STRETCH, true},
    };
    constexpr int kBuffer = 64;
    constexpr int kFrame = 11;
    Problems problems;
    const RECT client = ScreenCapture::client_on_screen(rig.window.hwnd());
    const int cw = static_cast<int>(client.right - client.left);
    const int ch = static_cast<int>(client.bottom - client.top);
    for (const Case& c : cases) {
        rig.chain.reset();
        drain(rig);
        SwapOptions chain_options = rig.options;
        chain_options.width = kBuffer;
        chain_options.height = kBuffer;
        chain_options.effect = c.effect;
        chain_options.buffers = c.buffers;
        chain_options.scaling = static_cast<int>(c.scaling);
        const HRESULT created = rig.try_create(chain_options, rig.window.hwnd(), rig.chain);
        rig.options = chain_options;
        context.measure(Kit::sanitize(c.name) + ".create_hr", Kit::hr(created));
        if (FAILED(created)) {
            if (!c.may_be_rejected || (created != DXGI_ERROR_INVALID_CALL && created != E_INVALIDARG && created != DXGI_ERROR_UNSUPPORTED)) {
                problems.add(std::string(c.name) + ": CreateSwapChainForHwnd returned " + Kit::hr(created));
            }
            continue;
        }
        if (!draw_quadrants(rig, draw, DXGI_FORMAT_UNKNOWN, kBuffer, kBuffer, kFrame, false, false, error)) {
            problems.add(std::string(c.name) + ": " + error);
            continue;
        }
        if (FAILED(rig.chain->Present(1, 0))) {
            problems.add(std::string(c.name) + ": Present failed");
            continue;
        }
        drain(rig);
        std::string detail;
        if (c.scaling == DXGI_SCALING_NONE) {
            Bitmap image;
            const std::vector<std::uint32_t> want = FrameImage::render(kFrame, kBuffer, kBuffer);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            bool matched = false;
            do {
                matched = ScreenCapture::screen(client, false, image, detail) && ScreenCapture::matches_region(image, 0, 0, kBuffer, kBuffer, want, 0, detail);
                if (!matched) {
                    ::Sleep(15);
                }
            } while (!matched && std::chrono::steady_clock::now() < deadline);
            if (!matched) {
                problems.add(std::string(c.name) + ": the " + std::to_string(kBuffer) + "x" + std::to_string(kBuffer) + " buffer is not shown 1:1 at the top-left of the " + std::to_string(cw) + "x" + std::to_string(ch) + " window: " + detail);
            } else {
                int rgb[3] = {};
                if (SwapRig::screen_rgb(static_cast<int>(client.left) + cw - 4, static_cast<int>(client.top) + ch - 4, rgb)) {
                    context.measure(Kit::sanitize(c.name) + ".outside_rgb", std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]));
                }
            }
            continue;
        }
        RECT content{0, 0, cw, ch};
        if (c.scaling == DXGI_SCALING_ASPECT_RATIO_STRETCH) {
            const int side = std::min(cw, ch);
            content = {(cw - side) / 2, (ch - side) / 2, (cw - side) / 2 + side, (ch - side) / 2 + side};
        }
        if (!ChainProbe::poll_quadrants(client, content, kFrame, 0, 3000, detail)) {
            problems.add(std::string(c.name) + ": the stretched content does not cover " + std::to_string(content.right - content.left) + "x" + std::to_string(content.bottom - content.top) + " at (" +
                         std::to_string(content.left) + "," + std::to_string(content.top) + "): " + detail);
        } else if (c.scaling == DXGI_SCALING_ASPECT_RATIO_STRETCH && content.left > 4) {
            int rgb[3] = {};
            if (SwapRig::screen_rgb(static_cast<int>(client.left) + 2, static_cast<int>(client.top) + ch / 2, rgb)) {
                context.measure("aspect.bar_rgb", std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]));
            }
        }
    }
    rig.chain.reset();
    drain(rig);
    draw.release();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
