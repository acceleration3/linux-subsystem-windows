#pragma once

#include "support/av_adapters.hpp"
#include "support/dyn_library.hpp"
#include "support/vulkan_table.hpp"
#include "support/vulkan_tools.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lsw::suites {
class VulkanRig;

template <typename T>
class VulkanOwned {
public:
    using Destroyer = void(VKAPI_PTR*)(VkDevice, T, const VkAllocationCallbacks*);

    VulkanOwned() = default;
    VulkanOwned(VulkanRig* rig, T handle, Destroyer destroyer) : rig_(rig), handle_(handle), destroyer_(destroyer) {}
    VulkanOwned(const VulkanOwned&) = delete;
    VulkanOwned& operator=(const VulkanOwned&) = delete;
    VulkanOwned(VulkanOwned&& other) noexcept { swap(other); }
    VulkanOwned& operator=(VulkanOwned&& other) noexcept {
        if (this != &other) {
            reset();
            swap(other);
        }
        return *this;
    }
    ~VulkanOwned() { reset(); }

    void reset();

    [[nodiscard]] T get() const { return handle_; }

    [[nodiscard]] bool valid() const { return handle_ != VK_NULL_HANDLE; }

    [[nodiscard]] T release() {
        const T value = handle_;
        handle_ = VK_NULL_HANDLE;
        return value;
    }

private:
    void swap(VulkanOwned& other) noexcept {
        std::swap(rig_, other.rig_);
        std::swap(handle_, other.handle_);
        std::swap(destroyer_, other.destroyer_);
    }

    VulkanRig* rig_ = nullptr;
    T handle_ = VK_NULL_HANDLE;
    Destroyer destroyer_ = nullptr;
};

struct VulkanQueue {
    std::uint32_t family = 0;
    std::uint32_t index = 0;
    VkQueue handle = nullptr;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkQueueFamilyProperties properties{};

    [[nodiscard]] bool supports(VkQueueFlags any_of) const { return (properties.queueFlags & any_of) != 0; }
};

struct VulkanRigOptions {
    std::vector<std::string> instance_extensions;
    std::vector<std::string> device_extensions;
    std::vector<std::string> layers;
    bool surface = false;
    bool debug_messages = false;
    std::uint32_t api_cap = VK_API_VERSION_1_3;
    std::uint32_t queues_per_family = 2;
};

class VulkanRig {
public:
    VulkanRig() = default;
    VulkanRig(const VulkanRig&) = delete;
    VulkanRig& operator=(const VulkanRig&) = delete;
    ~VulkanRig();

    [[nodiscard]] bool open(const AvAdapter& target, const VulkanRigOptions& options, std::string& error);

    [[nodiscard]] static std::vector<AvAdapter> target_adapters(std::string& error);

    [[nodiscard]] bool instance_has(const char* name) const { return instance_extensions.count(name) != 0; }

    [[nodiscard]] bool device_has(const char* name) const { return device_extensions.count(name) != 0; }

    [[nodiscard]] bool enabled_has(const char* name) const { return enabled_extensions.count(name) != 0; }

    [[nodiscard]] bool at_least(std::uint32_t version) const { return api >= version; }

    [[nodiscard]] VulkanQueue& main() { return queues[main_index]; }

    [[nodiscard]] std::vector<VulkanQueue*> queues_supporting(VkQueueFlags any_of);

    [[nodiscard]] int find_type(std::uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred = 0) const;

    [[nodiscard]] std::uint64_t heap_budget(std::uint32_t heap);

    [[nodiscard]] std::uint64_t heap_usage(std::uint32_t heap);

    [[nodiscard]] VkResult submit(const VulkanQueue& queue, std::uint32_t count, const VkSubmitInfo* infos, VkFence fence);

    [[nodiscard]] VkResult run(const std::function<void(VkCommandBuffer)>& record, VulkanQueue* queue = nullptr);

    [[nodiscard]] VkResult wait_fence(VkFence fence, std::uint64_t nanoseconds);

    VkResult wait_idle();

    void settle();

    void mark_pending() { pending_ = true; }

    template <typename T, typename Info, typename Create>
    [[nodiscard]] VkResult make(VulkanOwned<T>& out, Create create, typename VulkanOwned<T>::Destroyer destroyer, const Info& info) {
        T handle = VK_NULL_HANDLE;
        const VkResult result = create(device, &info, nullptr, &handle);
        if (result == VK_SUCCESS) {
            out = VulkanOwned<T>(this, handle, destroyer);
        }
        return result;
    }

    [[nodiscard]] VkResult make_fence(VulkanOwned<VkFence>& out, bool signaled = false);

    [[nodiscard]] VkResult make_semaphore(VulkanOwned<VkSemaphore>& out, bool timeline_type = false, std::uint64_t initial = 0);

    [[nodiscard]] VkResult make_event(VulkanOwned<VkEvent>& out);

    [[nodiscard]] VkResult make_shader(VulkanOwned<VkShaderModule>& out, const std::vector<std::uint32_t>& words);

    [[nodiscard]] VkResult make_query_pool(VulkanOwned<VkQueryPool>& out, VkQueryType type, std::uint32_t count,
                                           VkQueryPipelineStatisticFlags statistics = 0);

    [[nodiscard]] std::string describe() const;

    void drain_debug(std::string& first, int& errors, int& warnings);

    VulkanTable fn;
    DynLibrary loader;
    AvAdapter adapter;
    bool luid_matched = false;
    VkInstance instance = nullptr;
    VkPhysicalDevice physical = nullptr;
    VkDevice device = nullptr;
    std::uint32_t instance_api = 0;
    std::uint32_t api = 0;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceIDProperties id{};
    VkPhysicalDeviceMaintenance3Properties maintenance3{};
    VkPhysicalDeviceSubgroupProperties subgroup{};
    VkPhysicalDeviceTimelineSemaphoreProperties timeline_props{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::vector<VkQueueFamilyProperties> families;
    std::vector<VulkanQueue> queues;
    std::size_t main_index = 0;
    std::set<std::string> instance_extensions;
    std::set<std::string> instance_layers;
    std::set<std::string> device_extensions;
    std::set<std::string> enabled_extensions;
    bool timeline = false;
    bool sync2 = false;
    bool dynamic_rendering = false;
    bool memory_model = false;
    bool memory_model_device = false;
    bool host_query_reset = false;

private:
    std::atomic<bool> pending_{false};
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    std::mutex debug_mutex_;
    std::string debug_first_;
    int debug_errors_ = 0;
    int debug_warnings_ = 0;

    static VkBool32 VKAPI_PTR debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);
};

template <typename T>
void VulkanOwned<T>::reset() {
    if (handle_ != VK_NULL_HANDLE && rig_ != nullptr && destroyer_ != nullptr) {
        rig_->settle();
        destroyer_(rig_->device, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
}
}  // namespace lsw::suites
