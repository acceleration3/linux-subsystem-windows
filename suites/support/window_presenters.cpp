#include "support/window_presenter.hpp"

#include "support/api_policy.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/frame_image.hpp"
#include "support/gl_frame_draw.hpp"
#include "support/gl_rig.hpp"
#include "support/swap_rig.hpp"
#include "support/target_adapter.hpp"
#include "support/vulkan_swapchain.hpp"

#include <algorithm>

namespace lsw::suites {

namespace {

TestWindowConfig window_at(int x, int y, int width, int height) {
    TestWindowConfig config;
    config.x = x;
    config.y = y;
    TestWindow::outer_size(x, y, width, height, config.width, config.height);
    return config;
}

class D3d9Presenter final : public WindowPresenter {
public:
    [[nodiscard]] std::string name() const override { return "d3d9"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        width_ = width;
        height_ = height;
        D3d9Settings settings;
        settings.width = static_cast<UINT>(width);
        settings.height = static_cast<UINT>(height);
        settings.depth = false;
        settings.x = x;
        settings.y = y;
        return rig_.open(false, error) && rig_.create(settings, error);
    }

    [[nodiscard]] bool present(int frame, std::string& error) override {
        IDirect3DDevice9* device = rig_.dev();
        D3DRECT rects[4];
        for (int q = 0; q < 4; ++q) {
            const RECT r = FrameImage::quadrant(width_, height_, q);
            rects[0] = {r.left, r.top, r.right, r.bottom};
            if (FAILED(device->Clear(1, rects, D3DCLEAR_TARGET, FrameImage::color(frame, q), 1.0f, 0))) {
                error = "Clear of a quadrant failed";
                return false;
            }
        }
        const HRESULT presented = device->Present(nullptr, nullptr, nullptr, nullptr);
        if (FAILED(presented)) {
            error = "Present returned " + D3d9Tools::hr(presented);
            return false;
        }
        return D3d9Tools::wait_event(device, error);
    }

    void close() override {
        std::string error;
        if (rig_.dev() != nullptr) {
            static_cast<void>(D3d9Tools::wait_event(rig_.dev(), error));
        }
        rig_.close_device();
        rig_.window.destroy();
    }

    [[nodiscard]] HWND hwnd() const override { return rig_.window.hwnd(); }

    [[nodiscard]] bool window_dc_has_content() const override { return true; }

private:
    D3d9Rig rig_;
    int width_ = 0;
    int height_ = 0;
};

class D3d11Presenter final : public WindowPresenter {
public:
    explicit D3d11Presenter(bool flip) : flip_(flip) {}

    [[nodiscard]] std::string name() const override { return flip_ ? "d3d11 flip" : "d3d11"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        width_ = width;
        height_ = height;
        SwapOptions options;
        options.width = static_cast<UINT>(width);
        options.height = static_cast<UINT>(height);
        options.x = x;
        options.y = y;
        options.effect = flip_ ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_SEQUENTIAL;
        options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        return rig_.open(options, error) && draw_.open(rig_.device, error);
    }

    [[nodiscard]] bool present(int frame, std::string& error) override {
        Com<ID3D11RenderTargetView> view = rig_.back_view(0);
        if (!view) {
            error = "no render target view over buffer 0";
            return false;
        }
        draw_.frame(view.get(), width_, height_, frame);
        const HRESULT presented = rig_.chain->Present(1, 0);
        if (FAILED(presented)) {
            error = "Present returned " + D3d11Tools::hr(presented);
            return false;
        }
        return D3d11Tools::wait_event(rig_.device, rig_.device.context, error);
    }

    void close() override {
        std::string error;
        if (rig_.device.context != nullptr) {
            static_cast<void>(D3d11Tools::wait_event(rig_.device, rig_.device.context, error));
        }
        draw_.release();
        rig_.chain.reset();
        rig_.device.destroy();
        rig_.window.destroy();
    }

    [[nodiscard]] HWND hwnd() const override { return rig_.window.hwnd(); }

    [[nodiscard]] bool window_dc_has_content() const override { return !flip_; }

    [[nodiscard]] bool flip_model() const override { return flip_; }

private:
    bool flip_;
    SwapRig rig_;
    D3d11FrameDraw draw_;
    int width_ = 0;
    int height_ = 0;
};

class D3d12Presenter final : public WindowPresenter {
public:
    [[nodiscard]] std::string name() const override { return "d3d12"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        width_ = width;
        height_ = height;
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !rig_.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error) || !rig_.open_queues(error)) {
            return false;
        }
        if (!window_.create(window_at(x, y, width, height), error)) {
            return false;
        }
        ::SetWindowPos(window_.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        Com<IDXGISwapChain1> created;
        const HRESULT result = rig_.factory->CreateSwapChainForHwnd(rig_.direct().queue(), window_.hwnd(), &desc, nullptr, nullptr, created.put());
        if (FAILED(result)) {
            error = "CreateSwapChainForHwnd on a D3D12 queue returned " + D3d12Tools::hr(result);
            return false;
        }
        chain_ = created.as<IDXGISwapChain3>();
        rtvs_ = D3d12Tools::descriptor_heap(rig_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
        if (!chain_ || rtvs_ == nullptr) {
            error = "IDXGISwapChain3 or the RTV heap is unavailable";
            return false;
        }
        increment_ = rig_.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < 2; ++i) {
            Com<ID3D12Resource> buffer;
            if (FAILED(chain_->GetBuffer(i, __uuidof(ID3D12Resource), buffer.put_void()))) {
                error = "GetBuffer failed";
                return false;
            }
            buffers_[i] = rig_.hold(std::move(buffer));
            rig_.device()->CreateRenderTargetView(buffers_[i], nullptr, D3d12Tools::cpu_at(rtvs_, i, increment_));
        }
        return true;
    }

    [[nodiscard]] bool present(int frame, std::string& error) override {
        const UINT index = chain_->GetCurrentBackBufferIndex();
        std::uint64_t value = 0;
        if (index > 1 || !rig_.direct().run(
                             [&](ID3D12GraphicsCommandList* list) {
                                 D3d12Tools::move(list, buffers_[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                                 for (int q = 0; q < 4; ++q) {
                                     const std::uint32_t c = FrameImage::color(frame, q);
                                     const float color[4] = {static_cast<float>((c >> 16) & 255u) / 255.0f, static_cast<float>((c >> 8) & 255u) / 255.0f,
                                                             static_cast<float>(c & 255u) / 255.0f, 1.0f};
                                     const RECT r = FrameImage::quadrant(width_, height_, q);
                                     list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs_, index, increment_), color, 1, &r);
                                 }
                                 D3d12Tools::move(list, buffers_[index], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                             },
                             value, error)) {
            return false;
        }
        const HRESULT presented = chain_->Present(1, 0);
        if (FAILED(presented)) {
            error = "Present returned " + D3d12Tools::hr(presented);
            return false;
        }
        return rig_.direct().wait(value, error) && rig_.idle(error);
    }

    void close() override {
        std::string error;
        static_cast<void>(rig_.idle(error));
        for (ID3D12Resource*& buffer : buffers_) {
            if (buffer != nullptr) {
                rig_.release(buffer);
                buffer = nullptr;
            }
        }
        chain_.reset();
        rig_.shutdown();
        window_.destroy();
    }

    [[nodiscard]] HWND hwnd() const override { return window_.hwnd(); }

    [[nodiscard]] bool flip_model() const override { return true; }

private:
    TestWindow window_;
    D3d12Rig rig_;
    Com<IDXGISwapChain3> chain_;
    ID3D12DescriptorHeap* rtvs_ = nullptr;
    ID3D12Resource* buffers_[2] = {nullptr, nullptr};
    UINT increment_ = 0;
    int width_ = 0;
    int height_ = 0;
};

class GlPresenter final : public WindowPresenter {
public:
    [[nodiscard]] std::string name() const override { return "gl"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        width_ = width;
        height_ = height;
        GlRigOptions options;
        options.width = width;
        options.height = height;
        if (!rig_.open(options, error)) {
            return false;
        }
        rig_.window.move(x, y);
        ::SetWindowPos(rig_.window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        PIXELFORMATDESCRIPTOR described{};
        gdi_ = ::DescribePixelFormat(rig_.dc(), rig_.pixel_format, sizeof(described), &described) != 0 && (described.dwFlags & PFD_SUPPORT_GDI) != 0;
        return true;
    }

    [[nodiscard]] bool present(int frame, std::string& error) override {
        const GlApi& gl = rig_.gl;
        GlFrameDraw::frame(gl, width_, height_, frame);
        if (::SwapBuffers(rig_.dc()) == FALSE) {
            error = "SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")";
            return false;
        }
        gl.glFinish();
        return true;
    }

    void close() override {
        if (rig_.gl.glFinish != nullptr) {
            rig_.gl.glFinish();
        }
        GlContext::release();
        rig_.context.destroy();
        rig_.window.destroy();
    }

    [[nodiscard]] HWND hwnd() const override { return rig_.window.hwnd(); }

    [[nodiscard]] bool window_dc_has_content() const override { return gdi_; }

private:
    GlRig rig_;
    int width_ = 0;
    int height_ = 0;
    bool gdi_ = false;
};

class VulkanPresenter final : public WindowPresenter {
public:
    [[nodiscard]] std::string name() const override { return "vulkan"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        std::string target_error;
        const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(target_error);
        if (adapters.empty()) {
            error = "no Vulkan target adapter: " + target_error;
            return false;
        }
        VulkanRigOptions options;
        options.surface = true;
        if (!rig_.open(adapters.front(), options, error)) {
            return false;
        }
        if (!rig_.instance_has(vkw::kSurfaceExtension) || !rig_.enabled_has("VK_KHR_swapchain")) {
            error = "VK_KHR_win32_surface or VK_KHR_swapchain is not available";
            return false;
        }
        if (!window_.create(window_at(x, y, width, height), error)) {
            return false;
        }
        ::SetWindowPos(window_.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        VkResult result = chain_.open(rig_, window_.hwnd());
        if (result != VK_SUCCESS || !chain_.presentable()) {
            error = "the Win32 surface cannot be created or presented to: " + VulkanTools::result_name(result);
            return false;
        }
        VkSurfaceFormatKHR chosen{};
        bool found = false;
        for (const VkSurfaceFormatKHR& item : chain_.formats()) {
            if (item.format == VK_FORMAT_B8G8R8A8_UNORM || (!found && item.format == VK_FORMAT_R8G8B8A8_UNORM)) {
                chosen = item;
                found = true;
                if (item.format == VK_FORMAT_B8G8R8A8_UNORM) {
                    break;
                }
            }
        }
        if (!found) {
            error = "no UNORM 8-bit surface format";
            return false;
        }
        const VkSurfaceCapabilitiesKHR caps = chain_.capabilities();
        VkExtent2D extent{static_cast<std::uint32_t>(window_.width()), static_cast<std::uint32_t>(window_.height())};
        if (caps.currentExtent.width != 0xFFFFFFFFu) {
            extent = caps.currentExtent;
        }
        extent_ = extent;
        result = chain_.create(chosen.format, chosen.colorSpace, VK_PRESENT_MODE_FIFO_KHR, std::max(2u, caps.minImageCount), extent);
        if (result != VK_SUCCESS) {
            error = "vkCreateSwapchainKHR returned " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool present(int frame, std::string& error) override {
        std::uint32_t index = 0;
        VkResult result = chain_.acquire(5000000000ull, index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            error = "vkAcquireNextImageKHR returned " + VulkanTools::result_name(result);
            return false;
        }
        result = chain_.pixels_and_present(index, FrameImage::render(frame, static_cast<int>(extent_.width), static_cast<int>(extent_.height)));
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            error = "presenting returned " + VulkanTools::result_name(result);
            return false;
        }
        return rig_.wait_idle() == VK_SUCCESS;
    }

    void close() override {
        if (rig_.device != nullptr) {
            rig_.wait_idle();
        }
        chain_.destroy();
        window_.destroy();
    }

    [[nodiscard]] HWND hwnd() const override { return window_.hwnd(); }

    [[nodiscard]] bool flip_model() const override { return true; }

private:
    TestWindow window_;
    VulkanRig rig_;
    VulkanSwapchain chain_;
    VkExtent2D extent_{};
};

}  // namespace

std::vector<std::string> WindowPresenter::apis() {
    return ApiPolicy::filter({"d3d9", "d3d11", "d3d12", "gl", "vulkan"});
}

std::unique_ptr<WindowPresenter> WindowPresenter::make(const std::string& api, bool flip) {
    if (api == "d3d9") {
        return std::make_unique<D3d9Presenter>();
    }
    if (api == "d3d11") {
        return std::make_unique<D3d11Presenter>(flip);
    }
    if (api == "d3d12") {
        return std::make_unique<D3d12Presenter>();
    }
    if (api == "gl") {
        return std::make_unique<GlPresenter>();
    }
    if (api == "vulkan") {
        return std::make_unique<VulkanPresenter>();
    }
    return nullptr;
}

}  // namespace lsw::suites
