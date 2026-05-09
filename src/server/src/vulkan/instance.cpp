#include "lsw/server/vulkan/instance.hpp"

namespace lsw::server::vulkan {

Instance::~Instance() noexcept {
    destroy();
}

Instance::Instance(Instance&& other) noexcept : handle_(other.handle_) {
    other.handle_ = VK_NULL_HANDLE;
}

Instance& Instance::operator=(Instance&& other) noexcept {
    if (this != &other) {
        destroy();
        handle_ = other.handle_;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

VkInstance Instance::release() noexcept {
    VkInstance h = handle_;
    handle_ = VK_NULL_HANDLE;
    return h;
}

void Instance::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE) {
        ::vkDestroyInstance(handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

}  // namespace lsw::server::vulkan
