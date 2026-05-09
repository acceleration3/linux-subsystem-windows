#include "lsw/server/vulkan/device.hpp"

namespace lsw::server::vulkan {

Device::~Device() noexcept {
    destroy();
}

Device::Device(Device&& other) noexcept : handle_(other.handle_) {
    other.handle_ = VK_NULL_HANDLE;
}

Device& Device::operator=(Device&& other) noexcept {
    if (this != &other) {
        destroy();
        handle_ = other.handle_;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

VkDevice Device::release() noexcept {
    VkDevice h = handle_;
    handle_ = VK_NULL_HANDLE;
    return h;
}

void Device::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE) {
        ::vkDestroyDevice(handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

}  // namespace lsw::server::vulkan
