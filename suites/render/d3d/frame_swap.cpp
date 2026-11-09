#include "render/d3d/frame_swap.hpp"

#include "support/d3d11_tools.hpp"

namespace lsw::suites::render {

bool FrameSwap::open(IUnknown* device_or_queue, UINT width, UINT height, UINT buffers, DXGI_FORMAT format, bool allow_blit_fallback, std::string& error) {
    TestWindowConfig config;
    TestWindow::fit_config(config, static_cast<int>(width), static_cast<int>(height));
    if (!window_.create(config, error)) {
        return false;
    }
    Com<IDXGIFactory2> factory;
    HRESULT result = ::CreateDXGIFactory1(__uuidof(IDXGIFactory2), factory.put_void());
    if (FAILED(result)) {
        error = "IDXGIFactory2: " + D3d11Tools::hr(result);
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = buffers;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    result = factory->CreateSwapChainForHwnd(device_or_queue, window_.hwnd(), &desc, nullptr, nullptr, chain_.put());
    flip_ = SUCCEEDED(result);
    if (FAILED(result) && allow_blit_fallback) {
        desc.BufferCount = 1;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        result = factory->CreateSwapChainForHwnd(device_or_queue, window_.hwnd(), &desc, nullptr, nullptr, chain_.put());
    }
    if (FAILED(result) && allow_blit_fallback) {
        Com<IDXGIFactory> legacy;
        DXGI_SWAP_CHAIN_DESC old{};
        old.BufferDesc.Width = width;
        old.BufferDesc.Height = height;
        old.BufferDesc.Format = format;
        old.SampleDesc.Count = 1;
        old.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        old.BufferCount = 1;
        old.OutputWindow = window_.hwnd();
        old.Windowed = TRUE;
        old.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        Com<IDXGISwapChain> chain;
        result = factory->QueryInterface(__uuidof(IDXGIFactory), legacy.put_void());
        if (SUCCEEDED(result)) {
            result = legacy->CreateSwapChain(device_or_queue, &old, chain.put());
        }
        if (SUCCEEDED(result)) {
            result = chain->QueryInterface(__uuidof(IDXGISwapChain1), chain_.put_void());
        }
    }
    if (FAILED(result)) {
        error = "CreateSwapChainForHwnd: " + D3d11Tools::hr(result);
        return false;
    }
    factory->MakeWindowAssociation(window_.hwnd(), DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool FrameSwap::present(std::string& error) {
    const HRESULT result = chain_->Present(0, 0);
    if (FAILED(result)) {
        error = "Present: " + D3d11Tools::hr(result);
        return false;
    }
    return true;
}

}  // namespace lsw::suites::render
