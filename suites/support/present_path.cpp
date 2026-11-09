#include "support/present_path.hpp"

#include "support/api_policy.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_tools.hpp"
#include "support/frame_image.hpp"
#include "support/screen_capture.hpp"
#include "support/swap_rig.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>

namespace lsw::suites {

namespace {

PresentResult finished(HRESULT result, const std::string& name) {
    PresentResult out;
    out.hr = result;
    out.ok = SUCCEEDED(result);
    out.occluded = result == DXGI_STATUS_OCCLUDED;
    if (!out.ok) {
        out.error = "Present returned " + name;
    }
    return out;
}

PresentResult failed(const std::string& error) {
    PresentResult out;
    out.hr = E_FAIL;
    out.error = error;
    return out;
}

bool statistics_of(IDXGISwapChain* chain, FrameStats& out) {
    DXGI_FRAME_STATISTICS data{};
    if (chain == nullptr) {
        return false;
    }
    HRESULT result = chain->GetFrameStatistics(&data);
    if (result == DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
        result = chain->GetFrameStatistics(&data);
    }
    if (FAILED(result)) {
        return false;
    }
    out.present_count = data.PresentCount;
    out.present_refresh_count = data.PresentRefreshCount;
    out.sync_refresh_count = data.SyncRefreshCount;
    out.sync_qpc = data.SyncQPCTime.QuadPart;
    return true;
}

bool fit_window(TestWindow& window, int width, int height, std::string& error) {
    window.resize_client(width, height);
    if (window.width() != width || window.height() != height) {
        error = "the window client is " + std::to_string(window.width()) + "x" + std::to_string(window.height()) + " after asking for " + std::to_string(width) + "x" + std::to_string(height);
        return false;
    }
    return true;
}

class DxgiPath final : public PresentPath {
public:
    DxgiPath(std::string id, DXGI_SWAP_EFFECT effect, UINT buffers) : id_(std::move(id)), effect_(effect), buffers_(buffers) {}

    [[nodiscard]] std::string id() const override { return id_; }

    [[nodiscard]] std::string api() const override { return "d3d11"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        SwapOptions options;
        options.width = static_cast<UINT>(width);
        options.height = static_cast<UINT>(height);
        options.x = x;
        options.y = y;
        options.effect = effect_;
        options.buffers = buffers_;
        options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        if (!rig_.open(options, error) || !draw_.open(rig_.device, error)) {
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    [[nodiscard]] PresentResult present(int frame, UINT interval) override {
        Com<ID3D11RenderTargetView> view = rig_.back_view(0);
        if (!view) {
            return failed("no render target view over buffer 0");
        }
        draw_.frame(view.get(), width_, height_, frame);
        rig_.device.context->OMSetRenderTargets(0, nullptr, nullptr);
        view.reset();
        const HRESULT result = rig_.chain->Present(interval, 0);
        return finished(result, D3d11Tools::hr(result));
    }

    [[nodiscard]] bool supports_interval(UINT interval) const override { return interval <= 4; }

    [[nodiscard]] bool supports_image() const override { return true; }

    [[nodiscard]] PresentResult present_image(const std::vector<std::uint32_t>& xrgb, UINT interval) override {
        Com<ID3D11Texture2D> buffer = rig_.back_buffer(0);
        if (!buffer || xrgb.size() < static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)) {
            return failed("no back buffer or a short image");
        }
        std::vector<std::uint32_t> words(xrgb.size());
        for (std::size_t i = 0; i < xrgb.size(); ++i) {
            words[i] = 0xFF000000u | xrgb[i];
        }
        rig_.device.context->UpdateSubresource(buffer.get(), 0, nullptr, words.data(), static_cast<UINT>(width_) * 4u, 0);
        buffer.reset();
        const HRESULT result = rig_.chain->Present(interval, 0);
        return finished(result, D3d11Tools::hr(result));
    }

    [[nodiscard]] bool drain(std::string& error) override { return D3d11Tools::wait_event(rig_.device, rig_.device.context, error); }

    [[nodiscard]] bool resize(int width, int height, std::string& error) override {
        if (!drain(error)) {
            return false;
        }
        rig_.device.context->ClearState();
        rig_.device.context->Flush();
        if (!fit_window(rig_.window, width, height, error)) {
            return false;
        }
        const HRESULT result = rig_.chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, rig_.options.flags);
        if (FAILED(result)) {
            error = "ResizeBuffers returned " + D3d11Tools::hr(result);
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    void close() override {
        std::string error;
        if (rig_.device.context != nullptr) {
            static_cast<void>(drain(error));
        }
        draw_.release();
        rig_.chain.reset();
        rig_.device.destroy();
        rig_.window.destroy();
    }

    [[nodiscard]] HWND hwnd() const override { return rig_.window.hwnd(); }

    [[nodiscard]] bool flip_model() const override { return effect_ == DXGI_SWAP_EFFECT_FLIP_DISCARD || effect_ == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; }

    [[nodiscard]] bool frame_stats(FrameStats& out) override { return rig_.chain && statistics_of(rig_.chain.get(), out); }

    [[nodiscard]] bool last_present_count(std::uint64_t& out) override {
        UINT count = 0;
        if (!rig_.chain || FAILED(rig_.chain->GetLastPresentCount(&count))) {
            return false;
        }
        out = count;
        return true;
    }

private:
    std::string id_;
    DXGI_SWAP_EFFECT effect_;
    UINT buffers_;
    SwapRig rig_;
    D3d11FrameDraw draw_;
};

class D3d12Path final : public PresentPath {
public:
    static constexpr UINT kBuffers = 3;

    [[nodiscard]] std::string id() const override { return "d3d12_flip"; }

    [[nodiscard]] std::string api() const override { return "d3d12"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error) || !rig_.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error) || !rig_.open_queues(error)) {
            return false;
        }
        TestWindowConfig config;
        config.x = x;
        config.y = y;
        TestWindow::fit_config(config, width, height);
        if (!window_.create(config, error)) {
            return false;
        }
        ::SetWindowPos(window_.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kBuffers;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_NONE;
        Com<IDXGISwapChain1> created;
        const HRESULT result = rig_.factory->CreateSwapChainForHwnd(rig_.direct().queue(), window_.hwnd(), &desc, nullptr, nullptr, created.put());
        if (FAILED(result)) {
            error = "CreateSwapChainForHwnd on a D3D12 queue returned " + D3d12Tools::hr(result);
            return false;
        }
        chain_ = created.as<IDXGISwapChain3>();
        rtvs_ = D3d12Tools::descriptor_heap(rig_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBuffers, false);
        if (!chain_ || rtvs_ == nullptr) {
            error = "IDXGISwapChain3 or the RTV heap is unavailable";
            return false;
        }
        increment_ = rig_.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        width_ = width;
        height_ = height;
        return fetch_buffers(error);
    }

    [[nodiscard]] PresentResult present(int frame, UINT interval) override {
        const UINT index = chain_->GetCurrentBackBufferIndex();
        std::string error;
        if (index >= kBuffers || !wait_buffer(index, error)) {
            return failed(error.empty() ? "an unexpected back buffer index" : error);
        }
        std::uint64_t value = 0;
        const int w = width_;
        const int h = height_;
        if (!rig_.direct().run(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, buffers_[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                    for (int q = 0; q < 4; ++q) {
                        const std::uint32_t c = FrameImage::color(frame, q);
                        const float color[4] = {static_cast<float>((c >> 16) & 255u) / 255.0f, static_cast<float>((c >> 8) & 255u) / 255.0f,
                                                static_cast<float>(c & 255u) / 255.0f, 1.0f};
                        const RECT r = FrameImage::quadrant(w, h, q);
                        list->ClearRenderTargetView(D3d12Tools::cpu_at(rtvs_, index, increment_), color, 1, &r);
                    }
                    D3d12Tools::move(list, buffers_[index], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                },
                value, error)) {
            return failed(error);
        }
        last_value_[index] = value;
        const HRESULT result = chain_->Present(interval, 0);
        return finished(result, D3d12Tools::hr(result));
    }

    [[nodiscard]] bool supports_interval(UINT interval) const override { return interval <= 4; }

    [[nodiscard]] bool supports_image() const override { return true; }

    [[nodiscard]] PresentResult present_image(const std::vector<std::uint32_t>& xrgb, UINT interval) override {
        const UINT index = chain_->GetCurrentBackBufferIndex();
        std::string error;
        if (index >= kBuffers || !wait_buffer(index, error)) {
            return failed(error.empty() ? "an unexpected back buffer index" : error);
        }
        std::vector<std::uint32_t> words(xrgb.size());
        for (std::size_t i = 0; i < xrgb.size(); ++i) {
            words[i] = 0xFF000000u | xrgb[i];
        }
        if (!D3d12Tools::write_texture(rig_, buffers_[index], D3D12_RESOURCE_STATE_PRESENT, 0, words.data(), error)) {
            return failed(error);
        }
        last_value_[index] = 0;
        const HRESULT result = chain_->Present(interval, 0);
        return finished(result, D3d12Tools::hr(result));
    }

    [[nodiscard]] bool drain(std::string& error) override { return rig_.idle(error); }

    [[nodiscard]] bool resize(int width, int height, std::string& error) override {
        if (!rig_.idle(error)) {
            return false;
        }
        release_buffers();
        if (!fit_window(window_, width, height, error)) {
            return false;
        }
        const HRESULT result = chain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(result)) {
            error = "ResizeBuffers returned " + D3d12Tools::hr(result);
            return false;
        }
        width_ = width;
        height_ = height;
        return fetch_buffers(error);
    }

    void close() override {
        std::string error;
        static_cast<void>(rig_.idle(error));
        release_buffers();
        chain_.reset();
        rig_.shutdown();
        window_.destroy();
    }

    [[nodiscard]] HWND hwnd() const override { return window_.hwnd(); }

    [[nodiscard]] bool flip_model() const override { return true; }

    [[nodiscard]] bool frame_stats(FrameStats& out) override { return chain_ && statistics_of(chain_.get(), out); }

    [[nodiscard]] bool last_present_count(std::uint64_t& out) override {
        UINT count = 0;
        if (!chain_ || FAILED(chain_->GetLastPresentCount(&count))) {
            return false;
        }
        out = count;
        return true;
    }

private:
    [[nodiscard]] bool wait_buffer(UINT index, std::string& error) {
        const std::uint64_t value = last_value_[index];
        return value == 0 || rig_.direct().wait(value, error);
    }

    [[nodiscard]] bool fetch_buffers(std::string& error) {
        for (UINT i = 0; i < kBuffers; ++i) {
            Com<ID3D12Resource> buffer;
            if (FAILED(chain_->GetBuffer(i, __uuidof(ID3D12Resource), buffer.put_void()))) {
                error = "GetBuffer failed";
                return false;
            }
            buffers_[i] = rig_.hold(std::move(buffer));
            last_value_[i] = 0;
            rig_.device()->CreateRenderTargetView(buffers_[i], nullptr, D3d12Tools::cpu_at(rtvs_, i, increment_));
        }
        return true;
    }

    void release_buffers() {
        for (ID3D12Resource*& buffer : buffers_) {
            if (buffer != nullptr) {
                rig_.release(buffer);
                buffer = nullptr;
            }
        }
    }

    TestWindow window_;
    D3d12Rig rig_;
    Com<IDXGISwapChain3> chain_;
    ID3D12DescriptorHeap* rtvs_ = nullptr;
    ID3D12Resource* buffers_[kBuffers] = {};
    std::uint64_t last_value_[kBuffers] = {};
    UINT increment_ = 0;
};

}  // namespace

PresentResult PresentPath::present_image(const std::vector<std::uint32_t>&, UINT) {
    return failed("this path has no pixel upload");
}

bool PresentPath::frame_stats(FrameStats&) {
    return false;
}

bool PresentPath::last_present_count(std::uint64_t&) {
    return false;
}

RECT PresentPath::client() const {
    return ScreenCapture::client_on_screen(hwnd());
}

std::string PresentPath::api_of(const std::string& id) {
    if (id.rfind("d3d9", 0) == 0) {
        return "d3d9";
    }
    if (id.rfind("dxgi", 0) == 0) {
        return "d3d11";
    }
    if (id.rfind("d3d12", 0) == 0) {
        return "d3d12";
    }
    return id.rfind("gl", 0) == 0 ? "gl" : "vulkan";
}

std::vector<std::string> PresentPath::ids() {
    std::vector<std::string> out;
    for (const char* id : {"d3d9_blt", "d3d9ex_flip", "dxgi_blt_sequential", "dxgi_blt_discard", "dxgi_flip_sequential", "dxgi_flip_discard", "d3d12_flip",
                           "gl_swapbuffers", "vk_fifo", "vk_immediate", "vk_mailbox"}) {
        if (!ApiPolicy::is_skipped(api_of(id))) {
            out.push_back(id);
        }
    }
    return out;
}

std::vector<std::string> PresentPath::ids_of(const std::string& api) {
    std::vector<std::string> out;
    for (const std::string& id : ids()) {
        if (api_of(id) == api) {
            out.push_back(id);
        }
    }
    return out;
}

std::unique_ptr<PresentPath> PresentPath::make(const std::string& id) {
    if (id == "dxgi_blt_sequential") {
        return std::make_unique<DxgiPath>(id, DXGI_SWAP_EFFECT_SEQUENTIAL, 1);
    }
    if (id == "dxgi_blt_discard") {
        return std::make_unique<DxgiPath>(id, DXGI_SWAP_EFFECT_DISCARD, 1);
    }
    if (id == "dxgi_flip_sequential") {
        return std::make_unique<DxgiPath>(id, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 2);
    }
    if (id == "dxgi_flip_discard") {
        return std::make_unique<DxgiPath>(id, DXGI_SWAP_EFFECT_FLIP_DISCARD, 2);
    }
    if (id == "d3d12_flip") {
        return std::make_unique<D3d12Path>();
    }
    return make_native(id);
}

}  // namespace lsw::suites
