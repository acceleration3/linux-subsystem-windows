#include "support/vulkan_rig.hpp"

#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace lsw::suites {

namespace {

constexpr std::uint64_t kRunTimeoutNs = 30ull * 1000000000ull;

void collect_names(std::set<std::string>& out, const std::vector<VkExtensionProperties>& list) {
    for (const VkExtensionProperties& item : list) {
        out.insert(item.extensionName);
    }
}

}  // namespace

VkBool32 VKAPI_PTR VulkanRig::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    VulkanRig* rig = static_cast<VulkanRig*>(user);
    std::lock_guard lock(rig->debug_mutex_);
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        ++rig->debug_errors_;
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        ++rig->debug_warnings_;
    } else {
        return VK_FALSE;
    }
    if (rig->debug_first_.size() < 1500 && data != nullptr && data->pMessage != nullptr) {
        rig->debug_first_ += std::string(rig->debug_first_.empty() ? "" : " | ") + data->pMessage;
    }
    return VK_FALSE;
}

void VulkanRig::drain_debug(std::string& first, int& errors, int& warnings) {
    std::lock_guard lock(debug_mutex_);
    first = debug_first_;
    errors = debug_errors_;
    warnings = debug_warnings_;
}

VulkanRig::~VulkanRig() {
    if (device != nullptr && fn.vkDeviceWaitIdle != nullptr) {
        fn.vkDeviceWaitIdle(device);
    }
    if (device != nullptr) {
        for (VulkanQueue& queue : queues) {
            if (queue.pool != VK_NULL_HANDLE) {
                fn.vkDestroyCommandPool(device, queue.pool, nullptr);
            }
        }
        fn.vkDestroyDevice(device, nullptr);
    }
    if (instance != nullptr) {
        if (messenger_ != VK_NULL_HANDLE) {
            fn.vkDestroyDebugUtilsMessengerEXT(instance, messenger_, nullptr);
        }
        fn.vkDestroyInstance(instance, nullptr);
    }
}

std::vector<AvAdapter> VulkanRig::target_adapters(std::string& error) {
    std::vector<AvAdapter> out;
    if (std::getenv("LSW_SUITES_DEV_ANY") != nullptr) {
        AvAdapter one;
        if (TargetAdapter::pick(one, error)) {
            out.push_back(std::move(one));
        }
        return out;
    }
    if (!AvAdapters::accel(out, error)) {
        out.clear();
    }
    return out;
}

bool VulkanRig::open(const AvAdapter& target, const VulkanRigOptions& options, std::string& error) {
    adapter = target;
    if (!loader.load("vulkan-1.dll")) {
        error = "vulkan-1.dll cannot be loaded";
        return false;
    }
    if (!fn.load_global(loader.symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"))) {
        error = "the Vulkan loader does not resolve the global entry points";
        return false;
    }
    std::uint32_t loader_version = VK_API_VERSION_1_0;
    if (fn.vkEnumerateInstanceVersion != nullptr) {
        fn.vkEnumerateInstanceVersion(&loader_version);
    }
    if (loader_version < VK_API_VERSION_1_1) {
        error = "the Vulkan loader reports " + VulkanTools::version_text(loader_version) + ", below 1.1";
        return false;
    }
    instance_api = std::min(loader_version, options.api_cap);
    std::uint32_t count = 0;
    fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extension_list(count);
    fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, extension_list.data());
    collect_names(instance_extensions, extension_list);
    count = 0;
    fn.vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layer_list(count);
    fn.vkEnumerateInstanceLayerProperties(&count, layer_list.data());
    for (const VkLayerProperties& layer : layer_list) {
        instance_layers.insert(layer.layerName);
    }
    std::vector<std::string> wanted = options.instance_extensions;
    if (options.surface) {
        wanted.push_back("VK_KHR_surface");
        wanted.push_back(vkw::kSurfaceExtension);
    }
    if (options.debug_messages) {
        wanted.push_back("VK_EXT_debug_utils");
    }
    std::vector<std::string> enabled_instance;
    for (const std::string& name : wanted) {
        if (instance_has(name.c_str()) && std::find(enabled_instance.begin(), enabled_instance.end(), name) == enabled_instance.end()) {
            enabled_instance.push_back(name);
        }
    }
    std::vector<const char*> instance_names;
    for (const std::string& name : enabled_instance) {
        instance_names.push_back(name.c_str());
    }
    std::vector<const char*> layer_names;
    for (const std::string& name : options.layers) {
        if (instance_layers.count(name) == 0) {
            error = "layer " + name + " is not installed";
            return false;
        }
        layer_names.push_back(name.c_str());
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "lsw-suites";
    app.apiVersion = instance_api;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(instance_names.size());
    info.ppEnabledExtensionNames = instance_names.data();
    info.enabledLayerCount = static_cast<std::uint32_t>(layer_names.size());
    info.ppEnabledLayerNames = layer_names.data();
    VkResult result = fn.vkCreateInstance(&info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        instance = nullptr;
        error = "vkCreateInstance failed: " + VulkanTools::result_name(result);
        return false;
    }
    fn.load_instance(instance);
    if (fn.vkEnumeratePhysicalDevices == nullptr || fn.vkGetPhysicalDeviceProperties2 == nullptr ||
        fn.vkCreateDevice == nullptr || fn.vkGetDeviceProcAddr == nullptr) {
        error = "the instance does not resolve the physical device entry points";
        return false;
    }
    if (options.debug_messages && fn.vkCreateDebugUtilsMessengerEXT != nullptr) {
        VkDebugUtilsMessengerCreateInfoEXT messenger{};
        messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger.pfnUserCallback = &VulkanRig::debug_callback;
        messenger.pUserData = this;
        if (fn.vkCreateDebugUtilsMessengerEXT(instance, &messenger, nullptr, &messenger_) != VK_SUCCESS) {
            messenger_ = VK_NULL_HANDLE;
        }
    }
    count = 0;
    fn.vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    fn.vkEnumeratePhysicalDevices(instance, &count, devices.data());
    devices.resize(count);
    const bool any = std::getenv("LSW_SUITES_DEV_ANY") != nullptr;
    VkPhysicalDevice fallback = nullptr;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceIDProperties candidate_id{};
        candidate_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 candidate_props{};
        candidate_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        candidate_props.pNext = &candidate_id;
        fn.vkGetPhysicalDeviceProperties2(candidate, &candidate_props);
        if (candidate_id.deviceLUIDValid != VK_FALSE && std::memcmp(candidate_id.deviceLUID, &adapter.luid, sizeof(LUID)) == 0) {
            physical = candidate;
            luid_matched = true;
            break;
        }
        if (fallback == nullptr && candidate_props.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
            fallback = candidate;
        }
    }
    if (physical == nullptr && any) {
        physical = fallback;
    }
    if (physical == nullptr) {
        error = "no Vulkan physical device carries the LUID of " + adapter.name;
        return false;
    }
    id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    maintenance3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
    subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    timeline_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES;
    id.pNext = &maintenance3;
    maintenance3.pNext = &subgroup;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &id;
    count = 0;
    fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> device_list(count);
    fn.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, device_list.data());
    device_list.resize(count);
    collect_names(device_extensions, device_list);
    fn.vkGetPhysicalDeviceProperties(physical, &props);
    api = std::min(props.apiVersion, instance_api);
    const bool timeline_available = api >= VK_API_VERSION_1_2 || device_has("VK_KHR_timeline_semaphore");
    if (timeline_available) {
        maintenance3.pNext = &subgroup;
        subgroup.pNext = &timeline_props;
    }
    fn.vkGetPhysicalDeviceProperties2(physical, &properties2);
    fn.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    count = 0;
    fn.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    families.resize(count);
    fn.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    if (families.empty()) {
        error = "the physical device reports no queue family";
        return false;
    }
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features{};
    timeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    VkPhysicalDeviceSynchronization2Features sync2_features{};
    sync2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_features{};
    dynamic_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    VkPhysicalDeviceVulkanMemoryModelFeatures model_features{};
    model_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
    VkPhysicalDeviceHostQueryResetFeatures reset_features{};
    reset_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
    std::vector<std::string> extension_names;
    const auto want = [&](const std::string& name) {
        if (device_has(name.c_str()) && std::find(extension_names.begin(), extension_names.end(), name) == extension_names.end()) {
            extension_names.push_back(name);
        }
    };
    void* chain = nullptr;
    const auto link = [&](auto& feature) {
        feature.pNext = chain;
        chain = &feature;
    };
    if (timeline_available) {
        want("VK_KHR_timeline_semaphore");
        link(timeline_features);
    }
    if (api >= VK_API_VERSION_1_3 || device_has("VK_KHR_synchronization2")) {
        want("VK_KHR_synchronization2");
        link(sync2_features);
    }
    if (api >= VK_API_VERSION_1_3) {
        link(dynamic_features);
    }
    if (api >= VK_API_VERSION_1_2 || device_has("VK_KHR_vulkan_memory_model")) {
        want("VK_KHR_vulkan_memory_model");
        link(model_features);
    }
    if (api >= VK_API_VERSION_1_2 || device_has("VK_EXT_host_query_reset")) {
        want("VK_EXT_host_query_reset");
        link(reset_features);
    }
    want("VK_EXT_memory_budget");
    want("VK_KHR_push_descriptor");
    want("VK_EXT_calibrated_timestamps");
    want("VK_KHR_get_memory_requirements2");
    want("VK_KHR_descriptor_update_template");
    if (options.surface) {
        want("VK_KHR_swapchain");
    }
    for (const std::string& name : options.device_extensions) {
        want(name);
    }
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = chain;
    fn.vkGetPhysicalDeviceFeatures2(physical, &features2);
    features = features2.features;
    timeline = timeline_features.timelineSemaphore != VK_FALSE && timeline_available;
    sync2 = sync2_features.synchronization2 != VK_FALSE;
    dynamic_rendering = dynamic_features.dynamicRendering != VK_FALSE;
    memory_model = model_features.vulkanMemoryModel != VK_FALSE;
    memory_model_device = model_features.vulkanMemoryModelDeviceScope != VK_FALSE;
    host_query_reset = reset_features.hostQueryReset != VK_FALSE;
    features2.features.robustBufferAccess = VK_FALSE;
    std::size_t best_rank = 0;
    bool main_found = false;
    for (std::size_t i = 0; i < families.size(); ++i) {
        const VkQueueFlags flags = families[i].queueFlags;
        std::size_t rank = 0;
        if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0 && (flags & VK_QUEUE_COMPUTE_BIT) != 0) {
            rank = 3;
        } else if ((flags & VK_QUEUE_COMPUTE_BIT) != 0) {
            rank = 2;
        } else if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            rank = 1;
        }
        if (rank > best_rank) {
            best_rank = rank;
            main_index = i;
            main_found = true;
        }
    }
    if (!main_found) {
        error = "no queue family supports graphics or compute";
        return false;
    }
    std::vector<VkDeviceQueueCreateInfo> queue_infos(families.size());
    const std::vector<float> priorities(8, 1.0f);
    for (std::size_t i = 0; i < families.size(); ++i) {
        VkDeviceQueueCreateInfo& queue_info = queue_infos[i];
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = static_cast<std::uint32_t>(i);
        queue_info.queueCount = std::min({families[i].queueCount, options.queues_per_family, 8u});
        queue_info.pQueuePriorities = priorities.data();
    }
    std::vector<const char*> name_pointers;
    for (const std::string& name : extension_names) {
        name_pointers.push_back(name.c_str());
    }
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features2;
    device_info.queueCreateInfoCount = static_cast<std::uint32_t>(queue_infos.size());
    device_info.pQueueCreateInfos = queue_infos.data();
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(name_pointers.size());
    device_info.ppEnabledExtensionNames = name_pointers.data();
    result = fn.vkCreateDevice(physical, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        device = nullptr;
        error = "vkCreateDevice failed: " + VulkanTools::result_name(result);
        return false;
    }
    enabled_extensions.insert(extension_names.begin(), extension_names.end());
    fn.load_device(device);
    std::size_t main_slot = 0;
    for (std::size_t i = 0; i < queue_infos.size(); ++i) {
        for (std::uint32_t q = 0; q < queue_infos[i].queueCount; ++q) {
            VulkanQueue queue;
            queue.family = static_cast<std::uint32_t>(i);
            queue.index = q;
            queue.properties = families[i];
            fn.vkGetDeviceQueue(device, queue.family, q, &queue.handle);
            VkCommandPoolCreateInfo pool{};
            pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = queue.family;
            result = fn.vkCreateCommandPool(device, &pool, nullptr, &queue.pool);
            if (result != VK_SUCCESS) {
                error = "vkCreateCommandPool failed: " + VulkanTools::result_name(result);
                queue.pool = VK_NULL_HANDLE;
                queues.push_back(queue);
                return false;
            }
            if (i == main_index && q == 0) {
                main_slot = queues.size();
            }
            queues.push_back(queue);
        }
    }
    main_index = main_slot;
    return true;
}

std::vector<VulkanQueue*> VulkanRig::queues_supporting(VkQueueFlags any_of) {
    std::vector<VulkanQueue*> out;
    for (VulkanQueue& queue : queues) {
        if (queue.supports(any_of)) {
            out.push_back(&queue);
        }
    }
    return out;
}

int VulkanRig::find_type(std::uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const {
    int fallback = -1;
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) == 0 || (flags & required) != required) {
            continue;
        }
        if ((flags & preferred) == preferred) {
            return static_cast<int>(i);
        }
        if (fallback < 0) {
            fallback = static_cast<int>(i);
        }
    }
    return fallback;
}

namespace {

bool read_budget(VulkanRig& rig, VkPhysicalDeviceMemoryBudgetPropertiesEXT& budget) {
    if (!rig.enabled_has("VK_EXT_memory_budget") || rig.fn.vkGetPhysicalDeviceMemoryProperties2 == nullptr) {
        return false;
    }
    budget = {};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    props2.pNext = &budget;
    rig.fn.vkGetPhysicalDeviceMemoryProperties2(rig.physical, &props2);
    return true;
}

}  // namespace

std::uint64_t VulkanRig::heap_budget(std::uint32_t heap) {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    if (!read_budget(*this, budget) || heap >= memory.memoryHeapCount) {
        return memory.memoryHeaps[heap].size;
    }
    return budget.heapBudget[heap];
}

std::uint64_t VulkanRig::heap_usage(std::uint32_t heap) {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    if (!read_budget(*this, budget) || heap >= memory.memoryHeapCount) {
        return 0;
    }
    return budget.heapUsage[heap];
}

VkResult VulkanRig::submit(const VulkanQueue& queue, std::uint32_t count, const VkSubmitInfo* infos, VkFence fence) {
    pending_ = true;
    return fn.vkQueueSubmit(queue.handle, count, infos, fence);
}

VkResult VulkanRig::wait_fence(VkFence fence, std::uint64_t nanoseconds) {
    return fn.vkWaitForFences(device, 1, &fence, VK_TRUE, nanoseconds);
}

VkResult VulkanRig::wait_idle() {
    const VkResult result = fn.vkDeviceWaitIdle(device);
    pending_ = false;
    return result;
}

void VulkanRig::settle() {
    if (pending_.exchange(false)) {
        fn.vkDeviceWaitIdle(device);
    }
}

VkResult VulkanRig::run(const std::function<void(VkCommandBuffer)>& record, VulkanQueue* queue) {
    VulkanQueue& target = queue != nullptr ? *queue : main();
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = target.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer commands = nullptr;
    VkResult result = fn.vkAllocateCommandBuffers(device, &allocate, &commands);
    if (result != VK_SUCCESS) {
        return result;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = fn.vkBeginCommandBuffer(commands, &begin);
    if (result == VK_SUCCESS) {
        record(commands);
        result = fn.vkEndCommandBuffer(commands);
    }
    if (result != VK_SUCCESS) {
        fn.vkFreeCommandBuffers(device, target.pool, 1, &commands);
        return result;
    }
    VulkanOwned<VkFence> fence;
    result = make_fence(fence);
    if (result != VK_SUCCESS) {
        fn.vkFreeCommandBuffers(device, target.pool, 1, &commands);
        return result;
    }
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &commands;
    result = submit(target, 1, &submit_info, fence.get());
    if (result == VK_SUCCESS) {
        result = wait_fence(fence.get(), kRunTimeoutNs);
    }
    if (result == VK_SUCCESS) {
        fn.vkFreeCommandBuffers(device, target.pool, 1, &commands);
    }
    return result;
}

VkResult VulkanRig::make_fence(VulkanOwned<VkFence>& out, bool signaled) {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
    return make(out, fn.vkCreateFence, fn.vkDestroyFence, info);
}

VkResult VulkanRig::make_semaphore(VulkanOwned<VkSemaphore>& out, bool timeline_type, std::uint64_t initial) {
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = initial;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = timeline_type ? &type : nullptr;
    return make(out, fn.vkCreateSemaphore, fn.vkDestroySemaphore, info);
}

VkResult VulkanRig::make_event(VulkanOwned<VkEvent>& out) {
    VkEventCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
    return make(out, fn.vkCreateEvent, fn.vkDestroyEvent, info);
}

VkResult VulkanRig::make_shader(VulkanOwned<VkShaderModule>& out, const std::vector<std::uint32_t>& words) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();
    return make(out, fn.vkCreateShaderModule, fn.vkDestroyShaderModule, info);
}

VkResult VulkanRig::make_query_pool(VulkanOwned<VkQueryPool>& out, VkQueryType type, std::uint32_t count,
                                    VkQueryPipelineStatisticFlags statistics) {
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = type;
    info.queryCount = count;
    info.pipelineStatistics = statistics;
    return make(out, fn.vkCreateQueryPool, fn.vkDestroyQueryPool, info);
}

std::string VulkanRig::describe() const {
    return std::string(props.deviceName) + (adapter.has_outputs() ? " (display adapter)" : " (render-only adapter)") + ", Vulkan " +
           VulkanTools::version_text(api);
}

}  // namespace lsw::suites
