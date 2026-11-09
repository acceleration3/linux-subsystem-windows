#pragma once

#include "support/com_ptr.hpp"
#include "support/dyn_library.hpp"
#include "support/swap_rig.hpp"

#include <dcomp.h>
#include <dxgi1_3.h>

#include <string>

namespace lsw::suites {
class YuvVideoChain {
public:
    YuvVideoChain() = default;
    YuvVideoChain(const YuvVideoChain&) = delete;
    YuvVideoChain& operator=(const YuvVideoChain&) = delete;
    ~YuvVideoChain();

    [[nodiscard]] bool create(SwapRig& rig, HWND window, DXGI_FORMAT format, UINT width, UINT height, HRESULT& created, std::string& error);

    Com<IDXGISwapChain1> chain;

private:
    DynLibrary library_;
    HANDLE surface_ = nullptr;
    Com<IDCompositionDevice> device_;
    Com<IDCompositionTarget> target_;
    Com<IDCompositionVisual> visual_;
    Com<IUnknown> content_;
};
}  // namespace lsw::suites
