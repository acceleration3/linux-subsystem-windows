#include "render/platform/vk_platform.hpp"
#include "render/vk/kit.hpp"
#include "render/vk/presenter.hpp"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lsw::render::platform {

namespace {

class OffscreenPresenter : public vk::Presenter {
public:
    bool open(vk::Kit& kit, std::uint32_t width, std::uint32_t height, std::string& error) override {
        for (int i = 0; i < kCount; ++i) {
            vk::TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.format = VK_FORMAT_B8G8R8A8_UNORM;
            desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (!kit.make_texture(images_[i], desc, error)) {
                return false;
            }
        }
        return true;
    }

    VkFormat format() const override { return VK_FORMAT_B8G8R8A8_UNORM; }
    std::uint32_t image_count() const override { return kCount; }
    VkImage image(std::uint32_t index) const override { return images_[index].image; }
    VkImageLayout present_layout() const override { return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; }

    bool acquire(vk::Kit& kit, std::uint32_t& index, VkSemaphore signal, std::string& error) override {
        index = next_;
        next_ = (next_ + 1) % kCount;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signal;
        return kit.check(kit.api.vkQueueSubmit(kit.queue, 1, &submit, VK_NULL_HANDLE), "acquire", error);
    }

    bool present(vk::Kit& kit, std::uint32_t, VkSemaphore wait, std::string& error) override {
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &wait;
        submit.pWaitDstStageMask = &stage;
        return kit.check(kit.api.vkQueueSubmit(kit.queue, 1, &submit, VK_NULL_HANDLE), "present", error) &&
               kit.check(kit.api.vkQueueWaitIdle(kit.queue), "present wait", error);
    }

    void close(vk::Kit&) override {}

private:
    static constexpr int kCount = 3;
    vk::Texture images_[kCount];
    std::uint32_t next_ = 0;
};

std::string hex(const std::uint8_t* bytes, std::size_t count) {
    std::string out;
    char text[4];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(text, sizeof text, "%02x", bytes[i]);
        out += text;
    }
    return out;
}

class LinuxVkPlatform : public VkPlatform {
public:
    explicit LinuxVkPlatform(std::string selector) : selector_(std::move(selector)) {}

    ~LinuxVkPlatform() override {
        if (library_ != nullptr) {
            dlclose(library_);
        }
    }

    PFN_vkGetInstanceProcAddr loader(std::string& error) override {
        if (library_ == nullptr) {
            library_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        }
        if (library_ == nullptr) {
            error = "libvulkan.so.1 cannot be loaded";
            return nullptr;
        }
        void* symbol = dlsym(library_, "vkGetInstanceProcAddr");
        if (symbol == nullptr) {
            error = "libvulkan.so.1 exports no vkGetInstanceProcAddr";
        }
        return reinterpret_cast<PFN_vkGetInstanceProcAddr>(symbol);
    }

    std::vector<std::string> instance_extensions(bool) override { return {}; }
    std::vector<std::string> device_extensions(bool) override { return {}; }

    bool accepts(const VkDeviceIdentity& identity) override {
        if (selector_.empty() || selector_ == "auto") {
            return identity.index == 0;
        }
        if (selector_.rfind("uuid:", 0) == 0) {
            return hex(identity.uuid, 16) == selector_.substr(5);
        }
        if (selector_.rfind("index:", 0) == 0) {
            return identity.index == static_cast<std::uint32_t>(std::atoi(selector_.c_str() + 6));
        }
        unsigned vendor = 0;
        unsigned device = 0;
        if (std::sscanf(selector_.c_str(), "%x:%x", &vendor, &device) == 2) {
            return identity.vendor == vendor && identity.device == device;
        }
        return false;
    }

    std::unique_ptr<vk::Presenter> make_presenter(std::string&) override { return std::make_unique<OffscreenPresenter>(); }

private:
    std::string selector_;
    void* library_ = nullptr;
};

}  // namespace

std::unique_ptr<VkPlatform> make_vk_platform(const VkPlatformConfig& config, std::string&) {
    return std::make_unique<LinuxVkPlatform>(config.selector);
}

}  // namespace lsw::render::platform
