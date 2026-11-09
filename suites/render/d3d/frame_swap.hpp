#pragma once

#include "support/com_ptr.hpp"
#include "support/test_window.hpp"

#include <dxgi1_2.h>

#include <string>

namespace lsw::suites::render {
class FrameSwap {
public:
    [[nodiscard]] bool open(IUnknown* device_or_queue, UINT width, UINT height, UINT buffers, DXGI_FORMAT format, bool allow_blit_fallback,
                            std::string& error);

    [[nodiscard]] bool present(std::string& error);

    [[nodiscard]] bool flip_model() const { return flip_; }

    [[nodiscard]] IDXGISwapChain1* chain() const { return chain_.get(); }

private:
    TestWindow window_;
    Com<IDXGISwapChain1> chain_;
    bool flip_ = false;
};
}  // namespace lsw::suites::render
