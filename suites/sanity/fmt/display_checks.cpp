#include "sanity/fmt/display_checks.hpp"

#include "sanity/fmt/kit.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/swap_rig.hpp"

#include <cmath>
#include <cstdlib>

namespace lsw::suites::sanity::fmt {

namespace {

struct Candidate {
    DXGI_FORMAT format;
    bool screen;
};

const Candidate kCandidates[] = {{DXGI_FORMAT_B8G8R8A8_UNORM, true},
                                 {DXGI_FORMAT_R8G8B8A8_UNORM, true},
                                 {DXGI_FORMAT_R10G10B10A2_UNORM, true},
                                 {DXGI_FORMAT_R16G16B16A16_FLOAT, false},
                                 {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, false},
                                 {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false},
                                 {DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, false},
                                 {DXGI_FORMAT_B5G6R5_UNORM, false},
                                 {DXGI_FORMAT_B5G5R5A1_UNORM, false},
                                 {DXGI_FORMAT_R8_UNORM, false},
                                 {DXGI_FORMAT_R32_FLOAT, false},
                                 {DXGI_FORMAT_R16G16_FLOAT, false},
                                 {DXGI_FORMAT_R8G8_UNORM, false},
                                 {DXGI_FORMAT_R32G32B32A32_FLOAT, false}};

bool srgb_format(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

void dxgi_part(Context& context, Problems& problems) {
    SwapRig rig;
    SwapOptions options;
    options.width = 64;
    options.height = 64;
    std::string error;
    if (!rig.open(options, error)) {
        problems.add("DXGI: " + error);
        return;
    }
    Com<IDXGIDevice> dxgi_device;
    Com<IDXGIAdapter> dxgi_adapter;
    Com<IDXGIOutput> output;
    rig.device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    if (dxgi_device && SUCCEEDED(dxgi_device->GetAdapter(dxgi_adapter.put()))) {
        dxgi_adapter->EnumOutputs(0, output.put());
    }
    rig.chain.reset();
    int created = 0;
    int shown = 0;
    for (const Candidate& candidate : kCandidates) {
        const std::string name = std::string("DXGI ") + Kit::number_name(static_cast<int>(candidate.format));
        UINT bits = 0;
        if (FAILED(rig.device.device->CheckFormatSupport(candidate.format, &bits))) {
            bits = 0;
        }
        const bool display_bit = (bits & D3D11_FORMAT_SUPPORT_DISPLAY) != 0;
        if (output) {
            UINT modes = 0;
            const HRESULT listed = output->GetDisplayModeList(candidate.format, 0, &modes, nullptr);
            if (SUCCEEDED(listed) && modes != 0 && !display_bit) {
                problems.add(name + " is listed by GetDisplayModeList (" + std::to_string(modes) + " modes) but CheckFormatSupport does not report DISPLAY");
            }
        }
        SwapOptions chain_options = options;
        chain_options.format = candidate.format;
        chain_options.buffers = 2;
        chain_options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Com<IDXGISwapChain1> chain;
        HRESULT result = rig.try_create(chain_options, rig.window.hwnd(), chain);
        if (FAILED(result) && srgb_format(candidate.format)) {
            chain_options.buffers = 1;
            chain_options.effect = DXGI_SWAP_EFFECT_DISCARD;
            chain.reset();
            result = rig.try_create(chain_options, rig.window.hwnd(), chain);
        }
        if (display_bit && FAILED(result)) {
            problems.add(name + " reports DISPLAY support but a swap chain cannot be created: " + D3d11Tools::hr(result));
            continue;
        }
        if (!display_bit && SUCCEEDED(result)) {
            problems.add(name + " does not report DISPLAY support but a swap chain was created");
            chain.reset();
            continue;
        }
        if (!display_bit) {
            continue;
        }
        ++created;
        rig.chain = std::move(chain);
        rig.options = chain_options;
        const FormatSpec spec = FormatCodec::spec(candidate.format);
        Com<ID3D11Texture2D> buffer = rig.back_buffer(0);
        Com<ID3D11RenderTargetView> view = rig.back_view(0);
        if (!buffer || !view) {
            problems.add(name + ": the back buffer or its view cannot be reached");
            rig.chain.reset();
            continue;
        }
        D3d11Tools::clear(rig.device, view.get(), 0.2f, 0.4f, 0.6f, 1.0f);
        if (spec.known) {
            std::vector<std::uint8_t> bytes;
            if (!D3d11Tools::read_pixels(rig.device, buffer.get(), bytes, error)) {
                problems.add(name + ": reading the back buffer failed: " + error);
            } else {
                const double want[4] = {0.2, 0.4, 0.6, 1.0};
                std::string why;
                if (!FormatCodec::matches(spec, bytes.data(), want, why)) {
                    problems.add(name + ": the cleared back buffer holds the wrong colour: " + why);
                }
            }
        }
        const HRESULT presented = rig.chain->Present(1, 0);
        if (FAILED(presented)) {
            problems.add(name + ": Present failed: " + D3d11Tools::hr(presented));
        } else if (candidate.screen) {
            ++shown;
            const int want[3] = {51, 102, 153};
            int got[3] = {0, 0, 0};
            if (!rig.wait_for_color(32, 32, want, 5, 3000, got)) {
                problems.add(name + ": the presented colour on screen is (" + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," +
                             std::to_string(got[2]) + "), expected (51,102,153)");
            }
        }
        rig.device.context->ClearState();
        rig.device.context->Flush();
        rig.chain.reset();
    }
    context.measure("fmt.display.dxgi.chains", created);
    context.measure("fmt.display.dxgi.on_screen", shown);
}

void d3d9_part(Context& context, Problems& problems) {
    D3d9Rig rig;
    std::string error;
    if (!rig.open(false, error) || !rig.make_window(D3d9Settings{}, error)) {
        problems.add("D3D9: " + error);
        return;
    }
    IDirect3D9* d3d = rig.d3d.get();
    const struct {
        D3DFORMAT format;
        const char* name;
        bool readable;
    } formats[] = {{D3DFMT_X8R8G8B8, "X8R8G8B8", true}, {D3DFMT_A8R8G8B8, "A8R8G8B8", true}, {D3DFMT_R5G6B5, "R5G6B5", true},
                   {D3DFMT_X1R5G5B5, "X1R5G5B5", true}, {D3DFMT_A1R5G5B5, "A1R5G5B5", true}, {D3DFMT_A2R10G10B10, "A2R10G10B10", false},
                   {D3DFMT_A2B10G10R10, "A2B10G10R10", false}, {D3DFMT_A16B16G16R16F, "A16B16G16R16F", false}};
    int created = 0;
    for (const auto& item : formats) {
        const std::string name = std::string("D3D9 ") + item.name;
        const bool windowed_ok = d3d->CheckDeviceType(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, item.format, TRUE) == D3D_OK;
        const UINT modes = d3d->GetAdapterModeCount(rig.ordinal, item.format);
        if (modes != 0 && d3d->CheckDeviceType(rig.ordinal, D3DDEVTYPE_HAL, item.format, item.format, FALSE) != D3D_OK &&
            d3d->CheckDeviceType(rig.ordinal, D3DDEVTYPE_HAL, item.format, D3DFMT_X8R8G8B8, FALSE) != D3D_OK &&
            d3d->CheckDeviceType(rig.ordinal, D3DDEVTYPE_HAL, item.format, D3DFMT_A8R8G8B8, FALSE) != D3D_OK) {
            problems.add(name + " has " + std::to_string(modes) + " adapter modes but CheckDeviceType refuses every full-screen back buffer pairing");
        }
        if (!windowed_ok) {
            D3d9Settings settings;
            settings.back = item.format;
            settings.depth = false;
            const HRESULT result = rig.try_create(settings);
            if (SUCCEEDED(result)) {
                problems.add(name + " is refused by CheckDeviceType as a windowed back buffer but CreateDevice accepted it");
            }
            continue;
        }
        D3d9Settings settings;
        settings.back = item.format;
        settings.depth = false;
        const HRESULT result = rig.try_create(settings);
        if (FAILED(result)) {
            problems.add(name + " is accepted by CheckDeviceType as a windowed back buffer but CreateDevice failed: " + D3d9Tools::hr(result));
            continue;
        }
        ++created;
        const D3DCOLOR color = D3DCOLOR_ARGB(255, 51, 102, 153);
        D3d9Tools::clear(rig.dev(), color);
        if (item.readable) {
            Com<IDirect3DSurface9> back;
            SceneImage image;
            if (FAILED(rig.dev()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.put())) ||
                !D3d9Tools::read_surface(rig.dev(), back.get(), image, error)) {
                problems.add(name + ": reading the back buffer failed: " + error);
            } else {
                const std::uint32_t pixel = D3d9Tools::pixel(image, 4, 4);
                const int blue = static_cast<int>(pixel & 255);
                const int green = static_cast<int>((pixel >> 8) & 255);
                const int red = static_cast<int>((pixel >> 16) & 255);
                const int step = item.format == D3DFMT_R5G6B5 || item.format == D3DFMT_X1R5G5B5 || item.format == D3DFMT_A1R5G5B5 ? 9 : 1;
                if (std::abs(red - 51) > step || std::abs(green - 102) > step || std::abs(blue - 153) > step) {
                    problems.add(name + ": the cleared back buffer holds (" + std::to_string(red) + "," + std::to_string(green) + "," +
                                 std::to_string(blue) + "), expected (51,102,153)");
                }
            }
        }
        const HRESULT presented = rig.dev()->Present(nullptr, nullptr, nullptr, nullptr);
        if (FAILED(presented)) {
            problems.add(name + ": Present failed: " + D3d9Tools::hr(presented));
        }
    }
    context.measure("fmt.display.d3d9.devices", created);
}

}  // namespace

Outcome DisplayChecks::display_formats(Context& context) {
    Problems problems;
    bool exercised = false;
    std::vector<std::string> skips;
    if (Kit::enabled("d3d11")) {
        dxgi_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d9")) {
        d3d9_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D9 excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::fmt
