#include "sanity/present/alpha_checks.hpp"

#include "core/text.hpp"
#include "sanity/present/chain_probe.hpp"
#include "sanity/present/kit.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/dyn_library.hpp"
#include "support/swap_rig.hpp"

#include <dcomp.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 96;
constexpr int kFrame = 5;
constexpr std::uint32_t kBackground = 0x3C78C8u;
constexpr int kAlphas[4] = {64, 128, 192, 255};

using CreateDevice = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);

int channel(std::uint32_t value, int shift) {
    return static_cast<int>((value >> shift) & 255u);
}

int rounded(int numerator, int denominator) {
    return (numerator + denominator / 2) / denominator;
}

std::uint32_t composed(std::uint32_t colour, int alpha) {
    std::uint32_t out = 0;
    for (const int shift : {16, 8, 0}) {
        const int c = channel(colour, shift);
        const int source = rounded(c * alpha, 255);
        const int value = std::min(255, source + rounded((255 - alpha) * channel(kBackground, shift), 255));
        out |= static_cast<std::uint32_t>(value) << shift;
    }
    return out;
}

bool fill_chain(SwapRig& rig, D3d11FrameDraw& draw, IDXGISwapChain1* chain, int width, int height, const float rgba_for[4][4], std::string& error) {
    Com<ID3D11Texture2D> buffer;
    chain->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void());
    Com<ID3D11RenderTargetView> view;
    if (!buffer || FAILED(rig.device.device->CreateRenderTargetView(buffer.get(), nullptr, view.put()))) {
        error = "no render target view over the composition back buffer";
        return false;
    }
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        draw.fill_rgba(view.get(), FrameImage::quadrant(width, height, quadrant), rgba_for[quadrant]);
    }
    rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

struct Composition {
    Com<IDCompositionDevice> device;
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual> root;
    Com<IDCompositionVisual> back;
    Com<IDCompositionVisual> front;
};

}  // namespace

Outcome AlphaChecks::alpha_modes(Context& context) {
    SwapRig rig;
    SwapOptions options;
    options.width = kWidth;
    options.height = kHeight;
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
    float half[4][4];
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const std::uint32_t c = FrameImage::color(kFrame, quadrant);
        half[quadrant][0] = static_cast<float>(channel(c, 16)) / 255.0f;
        half[quadrant][1] = static_cast<float>(channel(c, 8)) / 255.0f;
        half[quadrant][2] = static_cast<float>(channel(c, 0)) / 255.0f;
        half[quadrant][3] = 0.5f;
    }
    const RECT client = ScreenCapture::client_on_screen(rig.window.hwnd());
    const RECT whole{0, 0, kWidth, kHeight};
    struct HwndMode {
        const char* name;
        DXGI_ALPHA_MODE mode;
    };
    for (const HwndMode& m : {HwndMode{"IGNORE", DXGI_ALPHA_MODE_IGNORE}, HwndMode{"PREMULTIPLIED", DXGI_ALPHA_MODE_PREMULTIPLIED}, HwndMode{"STRAIGHT", DXGI_ALPHA_MODE_STRAIGHT}}) {
        rig.chain.reset();
        SwapOptions chain_options = rig.options;
        chain_options.alpha = m.mode;
        const HRESULT created = rig.try_create(chain_options, rig.window.hwnd(), rig.chain);
        rig.options = chain_options;
        context.measure(std::string("hwnd.") + m.name + ".create_hr", Kit::hr(created));
        if (FAILED(created)) {
            if (m.mode == DXGI_ALPHA_MODE_IGNORE) {
                problems.add("a window swap chain with DXGI_ALPHA_MODE_IGNORE cannot be created: " + Kit::hr(created));
            }
            continue;
        }
        if (!fill_chain(rig, draw, rig.chain.get(), kWidth, kHeight, half, error) || FAILED(rig.chain->Present(1, 0))) {
            problems.add(std::string("window ") + m.name + ": " + (error.empty() ? "Present failed" : error));
            continue;
        }
        static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
        std::string detail;
        if (m.mode == DXGI_ALPHA_MODE_IGNORE) {
            if (!ChainProbe::poll_quadrants(client, whole, kFrame, 0, 3000, detail)) {
                problems.add("a window chain with DXGI_ALPHA_MODE_IGNORE and alpha 0.5 does not show the colour channels unchanged: " + detail);
            }
        } else {
            Bitmap image;
            if (ScreenCapture::screen(client, false, image, detail)) {
                context.measure(std::string("hwnd.") + m.name + ".quadrant0_rgb", ScreenCapture::rgb_text(image.at(kWidth / 4, kHeight / 4)));
            }
        }
    }
    rig.chain.reset();
    static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
    rig.window.destroy();
    DynLibrary library;
    CreateDevice create_device = library.load("dcomp.dll") ? library.symbol<CreateDevice>("DCompositionCreateDevice") : nullptr;
    if (create_device == nullptr) {
        draw.release();
        return Context::skip("DirectComposition (dcomp.dll) is not available, so composition swap chains cannot be composed against a known background");
    }
    Com<IDXGIDevice> dxgi_device;
    rig.device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    struct CompositionMode {
        const char* name;
        DXGI_ALPHA_MODE mode;
        bool required;
    };
    int composed_modes = 0;
    for (const CompositionMode& m : {CompositionMode{"PREMULTIPLIED", DXGI_ALPHA_MODE_PREMULTIPLIED, true}, CompositionMode{"STRAIGHT", DXGI_ALPHA_MODE_STRAIGHT, false},
                                     CompositionMode{"IGNORE", DXGI_ALPHA_MODE_IGNORE, true}}) {
        TestWindow window;
        TestWindowConfig config;
        config.x = Kit::kX;
        config.y = Kit::kY;
        TestWindow::outer_size(config.x, config.y, kWidth, kHeight, config.width, config.height);
        if (!window.create(config, error)) {
            problems.add(error);
            break;
        }
        ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Composition tree;
        if (FAILED(create_device(dxgi_device.get(), __uuidof(IDCompositionDevice), tree.device.put_void()))) {
            window.destroy();
            return Context::skip("DCompositionCreateDevice failed on the adapter's DXGI device");
        }
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = kWidth;
        desc.Height = kHeight;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.Scaling = DXGI_SCALING_STRETCH;
        Com<IDXGISwapChain1> back_chain;
        Com<IDXGISwapChain1> front_chain;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        const HRESULT back_created = rig.factory->CreateSwapChainForComposition(rig.device.device, &desc, nullptr, back_chain.put());
        desc.AlphaMode = m.mode;
        const HRESULT front_created = rig.factory->CreateSwapChainForComposition(rig.device.device, &desc, nullptr, front_chain.put());
        context.measure(std::string("composition.") + m.name + ".create_hr", Kit::hr(front_created));
        if (FAILED(back_created)) {
            window.destroy();
            return Context::skip("a composition swap chain cannot be created: " + Kit::hr(back_created));
        }
        if (FAILED(front_created)) {
            if (m.required) {
                problems.add(std::string("a composition swap chain with ") + m.name + " alpha cannot be created: " + Kit::hr(front_created));
            }
            window.destroy();
            continue;
        }
        float background[4][4];
        float source[4][4];
        for (int quadrant = 0; quadrant < 4; ++quadrant) {
            background[quadrant][0] = static_cast<float>(channel(kBackground, 16)) / 255.0f;
            background[quadrant][1] = static_cast<float>(channel(kBackground, 8)) / 255.0f;
            background[quadrant][2] = static_cast<float>(channel(kBackground, 0)) / 255.0f;
            background[quadrant][3] = 1.0f;
            const std::uint32_t c = FrameImage::color(kFrame, quadrant);
            const float alpha = static_cast<float>(kAlphas[quadrant]) / 255.0f;
            const float scale = m.mode == DXGI_ALPHA_MODE_PREMULTIPLIED ? alpha : 1.0f;
            source[quadrant][0] = static_cast<float>(channel(c, 16)) / 255.0f * scale;
            source[quadrant][1] = static_cast<float>(channel(c, 8)) / 255.0f * scale;
            source[quadrant][2] = static_cast<float>(channel(c, 0)) / 255.0f * scale;
            source[quadrant][3] = alpha;
        }
        if (!fill_chain(rig, draw, back_chain.get(), kWidth, kHeight, background, error) || !fill_chain(rig, draw, front_chain.get(), kWidth, kHeight, source, error)) {
            problems.add(std::string("composition ") + m.name + ": " + error);
            window.destroy();
            continue;
        }
        const bool built = SUCCEEDED(tree.device->CreateTargetForHwnd(window.hwnd(), TRUE, tree.target.put())) && SUCCEEDED(tree.device->CreateVisual(tree.root.put())) &&
                           SUCCEEDED(tree.device->CreateVisual(tree.back.put())) && SUCCEEDED(tree.device->CreateVisual(tree.front.put())) &&
                           SUCCEEDED(tree.back->SetContent(back_chain.get())) && SUCCEEDED(tree.front->SetContent(front_chain.get())) &&
                           SUCCEEDED(tree.root->AddVisual(tree.back.get(), FALSE, nullptr)) && SUCCEEDED(tree.root->AddVisual(tree.front.get(), TRUE, tree.back.get())) &&
                           SUCCEEDED(tree.target->SetRoot(tree.root.get()));
        if (!built) {
            problems.add(std::string("composition ") + m.name + ": the visual tree cannot be built");
            window.destroy();
            continue;
        }
        const HRESULT p1 = back_chain->Present(1, 0);
        const HRESULT p2 = front_chain->Present(1, 0);
        const HRESULT committed = tree.device->Commit();
        if (FAILED(p1) || FAILED(p2) || FAILED(committed)) {
            problems.add(std::string("composition ") + m.name + ": Present or Commit failed");
            window.destroy();
            continue;
        }
        static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
        ++composed_modes;
        const RECT area = ScreenCapture::client_on_screen(window.hwnd());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::string detail;
        bool matched = false;
        do {
            Bitmap image;
            matched = ScreenCapture::screen(area, false, image, detail);
            for (int quadrant = 0; matched && quadrant < 4; ++quadrant) {
                const RECT r = FrameImage::quadrant(kWidth, kHeight, quadrant);
                const int x = FrameImage::centre_x(r);
                const int y = FrameImage::centre_y(r);
                const std::uint32_t c = FrameImage::color(kFrame, quadrant) & 0xFFFFFFu;
                const int alpha = m.mode == DXGI_ALPHA_MODE_IGNORE ? 255 : kAlphas[quadrant];
                const std::uint32_t want = composed(c, alpha);
                if (!ChainProbe::within(image.at(x, y), want, 2)) {
                    matched = false;
                    detail = "quadrant " + std::to_string(quadrant) + " (alpha " + std::to_string(alpha) + ") is " + ScreenCapture::rgb_text(image.at(x, y)) + ", expected the source-over result " +
                             ScreenCapture::rgb_text(want) + " within 2 over the background " + ScreenCapture::rgb_text(kBackground);
                }
            }
            if (!matched) {
                ::Sleep(20);
            }
        } while (!matched && std::chrono::steady_clock::now() < deadline);
        if (!matched) {
            problems.add(std::string("composition chain with ") + m.name + " alpha composes wrongly: " + detail);
        }
        tree.target.reset();
        tree.front.reset();
        tree.back.reset();
        tree.root.reset();
        tree.device.reset();
        front_chain.reset();
        back_chain.reset();
        static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
        window.destroy();
    }
    draw.release();
    context.measure("composition_modes_composed", composed_modes);
    context.note("the layered-window alpha cases are covered by sanity.interop.gdi.layered_per_pixel_alpha");
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
