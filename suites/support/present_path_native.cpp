#include "support/present_path.hpp"

#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/frame_image.hpp"
#include "support/gl_frame_draw.hpp"
#include "support/gl_rig.hpp"
#include "support/screen_capture.hpp"
#include "support/vulkan_swapchain.hpp"

#include <algorithm>

namespace lsw::suites {

namespace {

PresentResult failed(const std::string& error, HRESULT result = E_FAIL) {
    PresentResult out;
    out.hr = result;
    out.error = error;
    return out;
}

bool fit_window(TestWindow& window, int width, int height, std::string& error) {
    window.resize_client(width, height);
    if (window.width() != width || window.height() != height) {
        error = "the window client is " + std::to_string(window.width()) + "x" + std::to_string(window.height()) + " after asking for " + std::to_string(width) + "x" + std::to_string(height);
        return false;
    }
    return true;
}

class D3d9Path final : public PresentPath {
public:
    explicit D3d9Path(bool flip) : flip_(flip) {}

    [[nodiscard]] std::string id() const override { return flip_ ? "d3d9ex_flip" : "d3d9_blt"; }

    [[nodiscard]] std::string api() const override { return "d3d9"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        D3d9Settings settings;
        settings.width = static_cast<UINT>(width);
        settings.height = static_cast<UINT>(height);
        settings.depth = false;
        settings.x = x;
        settings.y = y;
        settings.interval = flip_ ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_DEFAULT;
        if (flip_) {
            settings.effect = D3DSWAPEFFECT_FLIPEX;
            settings.buffers = 2;
        }
        if (!rig_.open(flip_, error) || !rig_.create(settings, error)) {
            return false;
        }
        settings_ = settings;
        width_ = width;
        height_ = height;
        return true;
    }

    [[nodiscard]] PresentResult present(int frame, UINT interval) override {
        std::string error;
        if (!set_interval(interval, error)) {
            return failed(error);
        }
        IDirect3DDevice9* device = rig_.dev();
        for (int q = 0; q < 4; ++q) {
            const RECT r = FrameImage::quadrant(width_, height_, q);
            const D3DRECT rect{r.left, r.top, r.right, r.bottom};
            if (FAILED(device->Clear(1, &rect, D3DCLEAR_TARGET, FrameImage::color(frame, q), 1.0f, 0))) {
                return failed("Clear of a quadrant failed");
            }
        }
        return finish();
    }

    [[nodiscard]] bool supports_interval(UINT interval) const override {
        D3DCAPS9 caps{};
        if (!rig_.caps(caps)) {
            return false;
        }
        if (interval == 0) {
            return (caps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE) != 0;
        }
        return interval == 1;
    }

    [[nodiscard]] bool supports_image() const override { return true; }

    [[nodiscard]] PresentResult present_image(const std::vector<std::uint32_t>& xrgb, UINT interval) override {
        std::string error;
        if (!set_interval(interval, error)) {
            return failed(error);
        }
        IDirect3DDevice9* device = rig_.dev();
        Com<IDirect3DSurface9> staged;
        Com<IDirect3DSurface9> uploaded;
        Com<IDirect3DSurface9> back;
        const UINT w = static_cast<UINT>(width_);
        const UINT h = static_cast<UINT>(height_);
        if (FAILED(device->CreateOffscreenPlainSurface(w, h, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, staged.put(), nullptr)) ||
            FAILED(device->CreateOffscreenPlainSurface(w, h, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, uploaded.put(), nullptr)) ||
            FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.put()))) {
            return failed("the upload surfaces cannot be created");
        }
        if (!D3d9Tools::write_surface(staged.get(), nullptr, xrgb, w, error)) {
            return failed(error);
        }
        if (FAILED(device->UpdateSurface(staged.get(), nullptr, uploaded.get(), nullptr)) ||
            FAILED(device->StretchRect(uploaded.get(), nullptr, back.get(), nullptr, D3DTEXF_NONE))) {
            return failed("UpdateSurface or StretchRect failed");
        }
        const PresentResult result = finish();
        std::string wait_error;
        static_cast<void>(D3d9Tools::wait_event(device, wait_error));
        return result;
    }

    [[nodiscard]] bool drain(std::string& error) override { return D3d9Tools::wait_event(rig_.dev(), error); }

    [[nodiscard]] bool resize(int width, int height, std::string& error) override {
        if (!drain(error) || !fit_window(rig_.window, width, height, error)) {
            return false;
        }
        D3d9Settings next = settings_;
        next.width = static_cast<UINT>(width);
        next.height = static_cast<UINT>(height);
        if (!reset(next, error)) {
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
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

    [[nodiscard]] bool flip_model() const override { return flip_; }

    [[nodiscard]] bool frame_stats(FrameStats& out) override {
        Com<IDirect3DSwapChain9Ex> chain = swap_chain();
        D3DPRESENTSTATS stats{};
        if (!chain || FAILED(chain->GetPresentStats(&stats))) {
            return false;
        }
        out.present_count = stats.PresentCount;
        out.present_refresh_count = stats.PresentRefreshCount;
        out.sync_refresh_count = stats.SyncRefreshCount;
        out.sync_qpc = stats.SyncQPCTime.QuadPart;
        return true;
    }

    [[nodiscard]] bool last_present_count(std::uint64_t& out) override {
        UINT count = 0;
        Com<IDirect3DSwapChain9Ex> chain = swap_chain();
        if (!chain || FAILED(chain->GetLastPresentCount(&count))) {
            return false;
        }
        out = count;
        return true;
    }

private:
    [[nodiscard]] Com<IDirect3DSwapChain9Ex> swap_chain() {
        Com<IDirect3DSwapChain9> base;
        if (!flip_ || !rig_.device_ex || FAILED(rig_.device_ex->GetSwapChain(0, base.put()))) {
            return Com<IDirect3DSwapChain9Ex>();
        }
        return base.as<IDirect3DSwapChain9Ex>();
    }

    [[nodiscard]] static DWORD interval_flag(UINT interval, bool flip) {
        if (interval == 0) {
            return D3DPRESENT_INTERVAL_IMMEDIATE;
        }
        if (interval == 2) {
            return D3DPRESENT_INTERVAL_TWO;
        }
        return flip ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_DEFAULT;
    }

    [[nodiscard]] bool reset(const D3d9Settings& next, std::string& error) {
        D3DPRESENT_PARAMETERS parameters = rig_.parameters(next);
        const HRESULT result = flip_ ? rig_.device_ex->ResetEx(&parameters, nullptr) : rig_.dev()->Reset(&parameters);
        if (FAILED(result)) {
            error = std::string(flip_ ? "ResetEx" : "Reset") + " returned " + D3d9Tools::hr(result);
            return false;
        }
        settings_ = next;
        return true;
    }

    [[nodiscard]] bool set_interval(UINT interval, std::string& error) {
        const DWORD wanted = interval_flag(interval, flip_);
        if (wanted == settings_.interval) {
            return true;
        }
        if (!supports_interval(interval)) {
            error = "presentation interval " + std::to_string(interval) + " is not supported";
            return false;
        }
        if (!drain(error)) {
            return false;
        }
        D3d9Settings next = settings_;
        next.interval = wanted;
        return reset(next, error);
    }

    [[nodiscard]] PresentResult finish() {
        PresentResult out;
        const HRESULT result = flip_ ? rig_.device_ex->PresentEx(nullptr, nullptr, nullptr, nullptr, 0) : rig_.dev()->Present(nullptr, nullptr, nullptr, nullptr);
        out.hr = result;
        out.ok = SUCCEEDED(result);
        out.occluded = result == S_PRESENT_OCCLUDED;
        if (!out.ok) {
            out.error = "Present returned " + D3d9Tools::hr(result);
        }
        return out;
    }

    bool flip_;
    D3d9Rig rig_;
    D3d9Settings settings_;
};

class GlPath final : public PresentPath {
public:
    [[nodiscard]] std::string id() const override { return "gl_swapbuffers"; }

    [[nodiscard]] std::string api() const override { return "gl"; }

    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error) override {
        GlRigOptions options;
        options.width = width;
        options.height = height;
        if (!rig_.open(options, error)) {
            return false;
        }
        rig_.window.move(x, y);
        ::SetWindowPos(rig_.window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        width_ = width;
        height_ = height;
        control_ = rig_.wgl.swap_interval != nullptr && rig_.wgl.get_swap_interval != nullptr &&
                   WglApi::contains(rig_.wgl.extensions(rig_.dc()), "WGL_EXT_swap_control");
        return true;
    }

    [[nodiscard]] PresentResult present(int frame, UINT interval) override {
        std::string error;
        if (!set_interval(interval, error)) {
            return failed(error);
        }
        GlFrameDraw::frame(rig_.gl, width_, height_, frame);
        return swap();
    }

    [[nodiscard]] bool supports_interval(UINT interval) const override { return control_ && interval <= 4; }

    [[nodiscard]] bool supports_image() const override {
        const GlApi& gl = rig_.gl;
        return gl.glBlitFramebuffer != nullptr && gl.glGenFramebuffers != nullptr && gl.glFramebufferTexture2D != nullptr && gl.glTexImage2D != nullptr;
    }

    [[nodiscard]] PresentResult present_image(const std::vector<std::uint32_t>& xrgb, UINT interval) override {
        std::string error;
        if (!set_interval(interval, error)) {
            return failed(error);
        }
        const GlApi& gl = rig_.gl;
        std::vector<std::uint32_t> words(xrgb.size());
        for (std::size_t i = 0; i < xrgb.size(); ++i) {
            words[i] = 0xFF000000u | xrgb[i];
        }
        unsigned texture = 0;
        unsigned fbo = 0;
        gl.glGenTextures(1, &texture);
        gl.glBindTexture(glc::TEXTURE_2D, texture);
        gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
        gl.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), width_, height_, 0, glc::BGRA, glc::UNSIGNED_BYTE, words.data());
        gl.glGenFramebuffers(1, &fbo);
        gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, fbo);
        gl.glFramebufferTexture2D(glc::READ_FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, texture, 0);
        gl.glBindFramebuffer(glc::DRAW_FRAMEBUFFER, 0);
        gl.glBlitFramebuffer(0, 0, width_, height_, 0, height_, width_, 0, glc::COLOR_BUFFER_BIT, glc::NEAREST);
        gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, 0);
        const PresentResult result = swap();
        gl.glFinish();
        gl.glDeleteFramebuffers(1, &fbo);
        gl.glDeleteTextures(1, &texture);
        return result;
    }

    [[nodiscard]] bool drain(std::string&) override {
        rig_.gl.glFinish();
        return true;
    }

    [[nodiscard]] bool resize(int width, int height, std::string& error) override {
        rig_.gl.glFinish();
        if (!fit_window(rig_.window, width, height, error)) {
            return false;
        }
        width_ = width;
        height_ = height;
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

    [[nodiscard]] bool flip_model() const override { return false; }

private:
    [[nodiscard]] bool set_interval(UINT interval, std::string& error) {
        if (!control_) {
            return true;
        }
        if (current_ == static_cast<int>(interval)) {
            return true;
        }
        if (rig_.wgl.swap_interval(static_cast<int>(interval)) == FALSE || rig_.wgl.get_swap_interval() != static_cast<int>(interval)) {
            error = "wglSwapIntervalEXT(" + std::to_string(interval) + ") did not take effect";
            return false;
        }
        current_ = static_cast<int>(interval);
        return true;
    }

    [[nodiscard]] PresentResult swap() {
        PresentResult out;
        if (::SwapBuffers(rig_.dc()) == FALSE) {
            out.error = "SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")";
            out.hr = HRESULT_FROM_WIN32(::GetLastError());
            return out;
        }
        out.ok = true;
        return out;
    }

    GlRig rig_;
    bool control_ = false;
    int current_ = -1;
};

class VulkanPath final : public PresentPath {
public:
    VulkanPath(std::string id, VkPresentModeKHR mode) : id_(std::move(id)), mode_(mode) {}

    [[nodiscard]] std::string id() const override { return id_; }

    [[nodiscard]] std::string api() const override { return "vulkan"; }

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
            error = "unsupported: VK_KHR_win32_surface or VK_KHR_swapchain is not available";
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
        VkResult result = chain_.open(rig_, window_.hwnd());
        if (result != VK_SUCCESS || !chain_.presentable()) {
            error = "unsupported: the Win32 surface cannot be created or presented to: " + VulkanTools::result_name(result);
            return false;
        }
        const std::vector<VkPresentModeKHR> modes = chain_.modes();
        if (std::find(modes.begin(), modes.end(), mode_) == modes.end()) {
            error = "unsupported: the surface does not report present mode " + std::to_string(static_cast<int>(mode_));
            return false;
        }
        bool found = false;
        for (const VkSurfaceFormatKHR& item : chain_.formats()) {
            if (item.format == VK_FORMAT_B8G8R8A8_UNORM) {
                format_ = item;
                found = true;
                break;
            }
        }
        if (!found) {
            error = "unsupported: no B8G8R8A8_UNORM surface format";
            return false;
        }
        width_ = width;
        height_ = height;
        return rebuild(error);
    }

    [[nodiscard]] PresentResult present(int frame, UINT) override {
        return send(FrameImage::render(frame, width_, height_));
    }

    [[nodiscard]] bool supports_interval(UINT interval) const override {
        return mode_ == VK_PRESENT_MODE_IMMEDIATE_KHR ? interval == 0 : interval == 1;
    }

    [[nodiscard]] bool supports_image() const override { return true; }

    [[nodiscard]] PresentResult present_image(const std::vector<std::uint32_t>& xrgb, UINT) override { return send(xrgb); }

    [[nodiscard]] bool drain(std::string&) override { return rig_.wait_idle() == VK_SUCCESS; }

    [[nodiscard]] bool resize(int width, int height, std::string& error) override {
        if (rig_.wait_idle() != VK_SUCCESS || !fit_window(window_, width, height, error)) {
            return false;
        }
        width_ = width;
        height_ = height;
        return rebuild(error);
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
    [[nodiscard]] bool rebuild(std::string& error) {
        const VkSurfaceCapabilitiesKHR caps = chain_.capabilities();
        VkExtent2D extent{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_)};
        if (caps.currentExtent.width != 0xFFFFFFFFu) {
            extent = caps.currentExtent;
        }
        if (static_cast<int>(extent.width) != width_ || static_cast<int>(extent.height) != height_) {
            error = "the surface extent is " + std::to_string(extent.width) + "x" + std::to_string(extent.height) + " for a " + std::to_string(width_) + "x" + std::to_string(height_) + " client";
            return false;
        }
        const std::uint32_t images = std::max(mode_ == VK_PRESENT_MODE_MAILBOX_KHR ? 3u : 2u, caps.minImageCount);
        const VkResult result = chain_.create(format_.format, format_.colorSpace, mode_, images, extent);
        if (result != VK_SUCCESS) {
            error = "vkCreateSwapchainKHR returned " + VulkanTools::result_name(result);
            return false;
        }
        return true;
    }

    [[nodiscard]] PresentResult send(const std::vector<std::uint32_t>& pixels) {
        std::uint32_t index = 0;
        VkResult result = chain_.acquire(5000000000ull, index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            PresentResult stale = failed("vkAcquireNextImageKHR returned VK_ERROR_OUT_OF_DATE_KHR");
            stale.stale = true;
            return stale;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return failed("vkAcquireNextImageKHR returned " + VulkanTools::result_name(result));
        }
        result = chain_.pixels_and_present(index, pixels);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            PresentResult stale = failed("presenting returned VK_ERROR_OUT_OF_DATE_KHR");
            stale.stale = true;
            return stale;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return failed("presenting returned " + VulkanTools::result_name(result));
        }
        PresentResult out;
        out.ok = true;
        return out;
    }

    std::string id_;
    VkPresentModeKHR mode_;
    TestWindow window_;
    VulkanRig rig_;
    VulkanSwapchain chain_;
    VkSurfaceFormatKHR format_{};
};

}  // namespace

std::unique_ptr<PresentPath> PresentPath::make_native(const std::string& id) {
    if (id == "d3d9_blt") {
        return std::make_unique<D3d9Path>(false);
    }
    if (id == "d3d9ex_flip") {
        return std::make_unique<D3d9Path>(true);
    }
    if (id == "gl_swapbuffers") {
        return std::make_unique<GlPath>();
    }
    if (id == "vk_fifo") {
        return std::make_unique<VulkanPath>(id, VK_PRESENT_MODE_FIFO_KHR);
    }
    if (id == "vk_immediate") {
        return std::make_unique<VulkanPath>(id, VK_PRESENT_MODE_IMMEDIATE_KHR);
    }
    if (id == "vk_mailbox") {
        return std::make_unique<VulkanPath>(id, VK_PRESENT_MODE_MAILBOX_KHR);
    }
    return nullptr;
}

}  // namespace lsw::suites
