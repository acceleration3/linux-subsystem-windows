#include "sanity/d3d10/dxgi_checks.hpp"

#include "sanity/d3d10/kit.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d9_tools.hpp"
#include "support/test_window.hpp"

namespace lsw::suites::sanity::d3d10 {

Outcome DxgiChecks::swapchain(Context& context) {
    Problems problems;
    for (const bool with_1 : {false, true}) {
        if (with_1 && !D3d10Device::library_available(true)) {
            continue;
        }
        AvAdapter adapter;
        D3d10Device d;
        std::string error;
        if (!Kit::open(adapter, d, with_1, error)) {
            return Outcome::fail(error);
        }
        const std::string name = Kit::name(with_1);
        TestWindowConfig config;
        config.x = 60;
        config.y = 60;
        config.width = 320;
        config.height = 280;
        TestWindow window;
        if (!window.create(config, error)) {
            return Outcome::fail(error);
        }
        ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Com<IDXGIDevice> dxgi_device = Kit::query<IDXGIDevice>(d.device);
        Com<IDXGIAdapter> dxgi_adapter;
        Com<IDXGIFactory> factory;
        if (!dxgi_device || FAILED(dxgi_device->GetAdapter(dxgi_adapter.put())) || FAILED(dxgi_adapter->GetParent(__uuidof(IDXGIFactory), factory.put_void()))) {
            return Outcome::fail("the DXGI factory of the D3D10 device cannot be reached");
        }
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = 200;
        desc.BufferDesc.Height = 160;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = window.hwnd();
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        Com<IDXGISwapChain> chain;
        const HRESULT created = factory->CreateSwapChain(d.device, &desc, chain.put());
        if (FAILED(created)) {
            problems.add(name + ": CreateSwapChain failed with " + Kit::hr(created));
            continue;
        }
        struct Stage {
            UINT width;
            UINT height;
            float colour[3];
            int bytes[3];
        };
        const Stage stages[2] = {{200, 160, {0.2f, 0.6f, 1.0f}, {51, 153, 255}}, {260, 200, {1.0f, 0.2f, 0.6f}, {255, 51, 153}}};
        for (int s = 0; s < 2; ++s) {
            const Stage& stage = stages[s];
            if (s == 1) {
                const HRESULT resized = chain->ResizeBuffers(0, stage.width, stage.height, DXGI_FORMAT_UNKNOWN, 0);
                if (FAILED(resized)) {
                    problems.add(name + ": ResizeBuffers failed with " + Kit::hr(resized));
                    break;
                }
                DXGI_SWAP_CHAIN_DESC now{};
                chain->GetDesc(&now);
                if (now.BufferDesc.Width != stage.width || now.BufferDesc.Height != stage.height || now.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                    problems.add(name + ": GetDesc reports " + std::to_string(now.BufferDesc.Width) + "x" + std::to_string(now.BufferDesc.Height) + " after ResizeBuffers");
                }
            }
            Com<ID3D10Texture2D> back;
            if (FAILED(chain->GetBuffer(0, __uuidof(ID3D10Texture2D), back.put_void()))) {
                problems.add(name + ": GetBuffer(0) failed");
                break;
            }
            D3D10_TEXTURE2D_DESC back_desc{};
            back->GetDesc(&back_desc);
            if (back_desc.Width != stage.width || back_desc.Height != stage.height) {
                problems.add(name + ": the back buffer is " + std::to_string(back_desc.Width) + "x" + std::to_string(back_desc.Height) + ", expected " + std::to_string(stage.width) + "x" + std::to_string(stage.height));
            }
            Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(d, back.get());
            D3d10Tools::clear(d, view.get(), stage.colour[0], stage.colour[1], stage.colour[2], 1.0f);
            TexelImage image;
            std::string where;
            if (!D3d10Tools::read_texture(d, back.get(), image, error)) {
                problems.add(name + ": " + error);
            } else if (!Kit::uniform(image, D3d10Tools::bgra(stage.bytes[0], stage.bytes[1], stage.bytes[2], 255), where)) {
                problems.add(name + ": the back buffer read through a staging copy is wrong: " + where);
            }
            const HRESULT present = chain->Present(0, 0);
            if (FAILED(present)) {
                problems.add(name + ": Present failed with " + Kit::hr(present));
            } else {
                int got[3] = {0, 0, 0};
                if (!D3d9Tools::wait_screen_color(window.hwnd(), 50, 50, stage.bytes, 2, 2500, got)) {
                    context.note(name + ": the window reads (" + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," + std::to_string(got[2]) + ") after Present of stage " + std::to_string(s));
                    problems.add(name + ": the presented colour of stage " + std::to_string(s) + " is not shown in the window");
                }
            }
            view.reset();
            back.reset();
            d.device->ClearState();
        }
    }
    return problems.outcome();
}

Outcome DxgiChecks::interop_d3d11(Context& context) {
    Problems problems;
    for (const bool with_1 : {false, true}) {
        if (with_1 && !D3d10Device::library_available(true)) {
            continue;
        }
        AvAdapter adapter;
        D3d10Device d10;
        D3d11Device d11;
        std::string error;
        if (!Kit::open(adapter, d10, with_1, error) || !d11.create(adapter.luid, error)) {
            return Outcome::fail(error);
        }
        const std::string name = Kit::name(with_1);
        auto rgba_word = [](std::uint32_t bgra_word) { return (bgra_word & 0xFF00FF00u) | ((bgra_word & 0xFFu) << 16) | ((bgra_word >> 16) & 0xFFu); };
        D3D10_TEXTURE2D_DESC desc = D3d10Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE);
        desc.MiscFlags = D3D10_RESOURCE_MISC_SHARED;
        HRESULT made = S_OK;
        Com<ID3D10Texture2D> shared = D3d10Tools::texture(d10, desc, nullptr, &made);
        Com<IDXGIResource> resource = Kit::query<IDXGIResource>(shared.get());
        HANDLE handle = nullptr;
        if (!shared || !resource || FAILED(resource->GetSharedHandle(&handle)) || handle == nullptr) {
            problems.add(name + ": a D3D10 texture created with MISC_SHARED has no shared handle (" + Kit::hr(made) + ")");
            continue;
        }
        Com<ID3D11Texture2D> opened;
        const HRESULT opened_result = d11.device->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), opened.put_void());
        if (FAILED(opened_result)) {
            problems.add(name + ": D3D11 cannot open the shared handle: " + D3d11Tools::hr(opened_result));
            continue;
        }
        D3D11_TEXTURE2D_DESC opened_desc{};
        opened->GetDesc(&opened_desc);
        if (opened_desc.Width != 64 || opened_desc.Height != 64 || opened_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
            problems.add(name + ": the opened texture has a different description");
        }
        Com<ID3D10RenderTargetView> view10 = D3d10Tools::rtv(d10, shared.get());
        Com<ID3D11RenderTargetView> view11 = D3d11Tools::rtv(d11, opened.get());
        for (int k = 0; k < 8; ++k) {
            float c[4];
            Kit::frame_floats(k, c);
            if (k % 2 == 0) {
                D3d10Tools::clear(d10, view10.get(), c[0], c[1], c[2], 1.0f);
                if (!d10.wait_idle(error)) {
                    return Outcome::fail(error);
                }
                TexelImage image;
                std::string where;
                if (!D3d11Tools::read_texture(d11, opened.get(), 0, image, error) || !Kit::uniform(image, rgba_word(Kit::frame_color(k)), where)) {
                    problems.add(name + ": frame " + std::to_string(k) + " written by D3D10 is not what D3D11 reads: " + where + error);
                }
            } else {
                D3d11Tools::clear(d11, view11.get(), c[0], c[1], c[2], 1.0f);
                if (!d11.wait_idle(error)) {
                    return Outcome::fail(error);
                }
                TexelImage image;
                std::string where;
                if (!D3d10Tools::read_texture(d10, shared.get(), image, error) || !Kit::uniform(image, Kit::frame_color(k), where)) {
                    problems.add(name + ": frame " + std::to_string(k) + " written by D3D11 is not what D3D10 reads: " + where + error);
                }
            }
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d10
