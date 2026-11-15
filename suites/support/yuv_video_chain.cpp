#include "support/yuv_video_chain.hpp"

namespace lsw::suites {

namespace {
using CreateDevice = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);
using CreateSurfaceHandle = HRESULT(WINAPI*)(DWORD, SECURITY_ATTRIBUTES*, HANDLE*);
}  // namespace

YuvVideoChain::~YuvVideoChain() {
    chain.reset();
    content_.reset();
    visual_.reset();
    target_.reset();
    device_.reset();
    if (surface_ != nullptr) {
        ::CloseHandle(surface_);
    }
}

bool YuvVideoChain::create(SwapRig& rig, HWND window, DXGI_FORMAT format, UINT width, UINT height, HRESULT& created, std::string& error) {
    created = E_FAIL;
    if (!library_.load("dcomp.dll")) {
        error = "dcomp.dll cannot be loaded";
        return false;
    }
    const CreateDevice create_device = library_.symbol<CreateDevice>("DCompositionCreateDevice");
    const CreateSurfaceHandle create_surface = library_.symbol<CreateSurfaceHandle>("DCompositionCreateSurfaceHandle");
    if (create_device == nullptr || create_surface == nullptr) {
        error = "dcomp.dll has no DCompositionCreateDevice or DCompositionCreateSurfaceHandle";
        return false;
    }
    Com<IDXGIDevice> dxgi_device;
    rig.device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    Com<IDXGIFactoryMedia> media = rig.factory.as<IDXGIFactoryMedia>();
    if (!dxgi_device || !media) {
        error = "the device has no DXGI device or the factory has no IDXGIFactoryMedia";
        return false;
    }
    if (FAILED(create_surface(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surface_))) {
        error = "DCompositionCreateSurfaceHandle failed";
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO;
    created = media->CreateSwapChainForCompositionSurfaceHandle(rig.device.device, surface_, &desc, nullptr, chain.put());
    if (FAILED(created)) {
        return true;
    }
    if (FAILED(create_device(dxgi_device.get(), __uuidof(IDCompositionDevice), device_.put_void())) || FAILED(device_->CreateTargetForHwnd(window, TRUE, target_.put())) ||
        FAILED(device_->CreateVisual(visual_.put())) || FAILED(device_->CreateSurfaceFromHandle(surface_, content_.put())) || FAILED(visual_->SetContent(content_.get())) ||
        FAILED(target_->SetRoot(visual_.get())) || FAILED(device_->Commit())) {
        error = "the video swap chain cannot be attached to a DirectComposition visual on the window";
        return false;
    }
    return true;
}

}  // namespace lsw::suites
