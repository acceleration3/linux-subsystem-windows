#include "sanity/vk/instance_checks.hpp"

#include "sanity/vk/kit.hpp"
#include "support/vulkan_extension_table.hpp"
#include "support/vulkan_image_io.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>

namespace lsw::suites::sanity::vk {

namespace {

struct FeatureName {
    const char* name;
    std::size_t index;
};

const char* const kFeatureNames[] = {
    "robustBufferAccess", "fullDrawIndexUint32", "imageCubeArray", "independentBlend", "geometryShader",
    "tessellationShader", "sampleRateShading", "dualSrcBlend", "logicOp", "multiDrawIndirect",
    "drawIndirectFirstInstance", "depthClamp", "depthBiasClamp", "fillModeNonSolid", "depthBounds",
    "wideLines", "largePoints", "alphaToOne", "multiViewport", "samplerAnisotropy",
    "textureCompressionETC2", "textureCompressionASTC_LDR", "textureCompressionBC", "occlusionQueryPrecise",
    "pipelineStatisticsQuery", "vertexPipelineStoresAndAtomics", "fragmentStoresAndAtomics",
    "shaderTessellationAndGeometryPointSize", "shaderImageGatherExtended", "shaderStorageImageExtendedFormats",
    "shaderStorageImageMultisample", "shaderStorageImageReadWithoutFormat", "shaderStorageImageWriteWithoutFormat",
    "shaderUniformBufferArrayDynamicIndexing", "shaderSampledImageArrayDynamicIndexing",
    "shaderStorageBufferArrayDynamicIndexing", "shaderStorageImageArrayDynamicIndexing", "shaderClipDistance",
    "shaderCullDistance", "shaderFloat64", "shaderInt64", "shaderInt16", "shaderResourceResidency",
    "shaderResourceMinLod", "sparseBinding", "sparseResidencyBuffer", "sparseResidencyImage2D",
    "sparseResidencyImage3D", "sparseResidency2Samples", "sparseResidency4Samples", "sparseResidency8Samples",
    "sparseResidency16Samples", "sparseResidencyAliased", "variableMultisampleRate", "inheritedQueries"};

constexpr std::size_t kFeatureCount = sizeof(kFeatureNames) / sizeof(kFeatureNames[0]);

static_assert(kFeatureCount * sizeof(VkBool32) == sizeof(VkPhysicalDeviceFeatures), "feature table out of date");

const VkBool32* feature_array(const VkPhysicalDeviceFeatures& features) {
    return reinterpret_cast<const VkBool32*>(&features);
}

VkResult create_with(VulkanRig& rig, const VkPhysicalDeviceFeatures* features, const char* extension, VkDevice& device) {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = rig.main().family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.pEnabledFeatures = features;
    info.enabledExtensionCount = extension != nullptr ? 1 : 0;
    info.ppEnabledExtensionNames = &extension;
    device = nullptr;
    return rig.fn.vkCreateDevice(rig.physical, &info, nullptr, &device);
}

void drop_device(VulkanRig& rig, VkDevice device) {
    if (device == nullptr) {
        return;
    }
    const auto destroy = reinterpret_cast<PFN_vkDestroyDevice>(rig.fn.vkGetDeviceProcAddr(device, "vkDestroyDevice"));
    if (destroy != nullptr) {
        destroy(device, nullptr);
    }
}


bool limits_equal(const VkPhysicalDeviceLimits& a, const VkPhysicalDeviceLimits& b) {
    return a.maxImageDimension1D == b.maxImageDimension1D && a.maxImageDimension2D == b.maxImageDimension2D &&
           a.maxImageDimension3D == b.maxImageDimension3D && a.maxImageDimensionCube == b.maxImageDimensionCube &&
           a.maxImageArrayLayers == b.maxImageArrayLayers && a.maxTexelBufferElements == b.maxTexelBufferElements &&
           a.maxUniformBufferRange == b.maxUniformBufferRange && a.maxStorageBufferRange == b.maxStorageBufferRange &&
           a.maxPushConstantsSize == b.maxPushConstantsSize && a.maxMemoryAllocationCount == b.maxMemoryAllocationCount &&
           a.bufferImageGranularity == b.bufferImageGranularity && a.sparseAddressSpaceSize == b.sparseAddressSpaceSize &&
           a.maxBoundDescriptorSets == b.maxBoundDescriptorSets && a.maxPerStageResources == b.maxPerStageResources &&
           a.maxVertexInputAttributes == b.maxVertexInputAttributes && a.maxComputeSharedMemorySize == b.maxComputeSharedMemorySize &&
           a.maxComputeWorkGroupInvocations == b.maxComputeWorkGroupInvocations &&
           a.maxComputeWorkGroupSize[0] == b.maxComputeWorkGroupSize[0] && a.maxViewports == b.maxViewports &&
           a.maxViewportDimensions[0] == b.maxViewportDimensions[0] && a.minMemoryMapAlignment == b.minMemoryMapAlignment &&
           a.minUniformBufferOffsetAlignment == b.minUniformBufferOffsetAlignment &&
           a.minStorageBufferOffsetAlignment == b.minStorageBufferOffsetAlignment &&
           a.nonCoherentAtomSize == b.nonCoherentAtomSize && a.timestampPeriod == b.timestampPeriod &&
           a.maxColorAttachments == b.maxColorAttachments && a.maxSamplerAnisotropy == b.maxSamplerAnisotropy &&
           a.framebufferColorSampleCounts == b.framebufferColorSampleCounts && a.timestampComputeAndGraphics == b.timestampComputeAndGraphics &&
           a.optimalBufferCopyOffsetAlignment == b.optimalBufferCopyOffsetAlignment &&
           a.optimalBufferCopyRowPitchAlignment == b.optimalBufferCopyRowPitchAlignment &&
           a.lineWidthRange[1] == b.lineWidthRange[1] && a.pointSizeRange[1] == b.pointSizeRange[1];
}

template <typename T>
bool bytes_equal(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

}  // namespace

Outcome InstanceChecks::version_layers_extensions(Context& context) {
    DynLibrary loader;
    VulkanTable fn;
    if (!loader.load("vulkan-1.dll")) {
        return Outcome::fail("vulkan-1.dll cannot be loaded");
    }
    if (!fn.load_global(loader.symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"))) {
        return Outcome::fail("the Vulkan loader does not resolve vkCreateInstance and the instance enumeration entry points");
    }
    Problems problems;
    if (fn.vkEnumerateInstanceVersion == nullptr) {
        return Outcome::fail("vkEnumerateInstanceVersion does not resolve, so the loader is older than Vulkan 1.1");
    }
    std::uint32_t version = 0;
    Kit::succeeds(problems, "vkEnumerateInstanceVersion", fn.vkEnumerateInstanceVersion(&version));
    context.measure("vk.instance_version", VulkanTools::version_text(version));
    if (version < VK_API_VERSION_1_1) {
        problems.add("vkEnumerateInstanceVersion reports " + VulkanTools::version_text(version) + ", below 1.1");
    }
    std::uint32_t count = 0;
    Kit::succeeds(problems, "vkEnumerateInstanceExtensionProperties (count)", fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> extensions(count);
    Kit::succeeds(problems, "vkEnumerateInstanceExtensionProperties (fill)",
                  fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()));
    extensions.resize(count);
    std::set<std::string> names;
    for (const VkExtensionProperties& extension : extensions) {
        if (!names.insert(extension.extensionName).second) {
            problems.add(std::string("instance extension ") + extension.extensionName + " is listed twice");
        }
        if (extension.specVersion == 0) {
            problems.add(std::string("instance extension ") + extension.extensionName + " has specVersion 0");
        }
    }
    if (count > 1) {
        std::uint32_t small = 1;
        VkExtensionProperties one{};
        Kit::expect(problems, "vkEnumerateInstanceExtensionProperties with room for one of " + std::to_string(count),
                    fn.vkEnumerateInstanceExtensionProperties(nullptr, &small, &one), {VK_INCOMPLETE});
        if (small != 1) {
            problems.add("the VK_INCOMPLETE enumeration returned count " + std::to_string(small) + " instead of 1");
        }
    }
    std::uint32_t big = count + 8;
    std::vector<VkExtensionProperties> roomy(big);
    Kit::succeeds(problems, "vkEnumerateInstanceExtensionProperties with extra room",
                  fn.vkEnumerateInstanceExtensionProperties(nullptr, &big, roomy.data()));
    if (big != count) {
        problems.add("a larger buffer changed the extension count from " + std::to_string(count) + " to " + std::to_string(big));
    }
    std::uint32_t layer_count = 0;
    Kit::succeeds(problems, "vkEnumerateInstanceLayerProperties (count)", fn.vkEnumerateInstanceLayerProperties(&layer_count, nullptr));
    std::vector<VkLayerProperties> layers(layer_count);
    Kit::succeeds(problems, "vkEnumerateInstanceLayerProperties (fill)",
                  fn.vkEnumerateInstanceLayerProperties(&layer_count, layers.data()));
    if (layer_count > 1) {
        std::uint32_t small = 1;
        VkLayerProperties one{};
        Kit::expect(problems, "vkEnumerateInstanceLayerProperties with room for one of " + std::to_string(layer_count),
                    fn.vkEnumerateInstanceLayerProperties(&small, &one), {VK_INCOMPLETE});
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "lsw-suites";
    app.apiVersion = version;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    const char* bad_extension = "VK_LSW_extension_that_does_not_exist";
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &bad_extension;
    VkInstance instance = nullptr;
    VkResult result = fn.vkCreateInstance(&info, nullptr, &instance);
    Kit::expect(problems, "vkCreateInstance with an unsupported extension", result, {VK_ERROR_EXTENSION_NOT_PRESENT});
    if (result == VK_SUCCESS && instance != nullptr) {
        fn.load_instance(instance);
        fn.vkDestroyInstance(instance, nullptr);
    }
    const char* bad_layer = "VK_LAYER_LSW_layer_that_does_not_exist";
    info.enabledExtensionCount = 0;
    info.ppEnabledExtensionNames = nullptr;
    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = &bad_layer;
    instance = nullptr;
    result = fn.vkCreateInstance(&info, nullptr, &instance);
    Kit::expect(problems, "vkCreateInstance with an unsupported layer", result, {VK_ERROR_LAYER_NOT_PRESENT});
    if (result == VK_SUCCESS && instance != nullptr) {
        fn.load_instance(instance);
        fn.vkDestroyInstance(instance, nullptr);
    }
    std::vector<const char*> all;
    for (const VkExtensionProperties& extension : extensions) {
        all.push_back(extension.extensionName);
    }
    info.enabledLayerCount = 0;
    info.ppEnabledLayerNames = nullptr;
    info.enabledExtensionCount = static_cast<std::uint32_t>(all.size());
    info.ppEnabledExtensionNames = all.data();
    instance = nullptr;
    result = fn.vkCreateInstance(&info, nullptr, &instance);
    if (!Kit::succeeds(problems, "vkCreateInstance at the reported apiVersion with every advertised instance extension", result)) {
        return problems.outcome();
    }
    fn.load_instance(instance);
    std::size_t resolved = 0;
    for (const std::string& extension : names) {
        for (const std::string& function : VulkanExtensionTable::instance_functions(extension)) {
            if (fn.vkGetInstanceProcAddr(instance, function.c_str()) == nullptr) {
                problems.add(extension + " is advertised but " + function + " does not resolve");
            } else {
                ++resolved;
            }
        }
    }
    context.measure("vk.instance_functions_resolved", resolved);
    context.measure("vk.instance_extensions", names.size());
    std::uint32_t device_count = 0;
    Kit::succeeds(problems, "vkEnumeratePhysicalDevices (count)", fn.vkEnumeratePhysicalDevices(instance, &device_count, nullptr));
    std::vector<VkPhysicalDevice> devices(device_count);
    Kit::succeeds(problems, "vkEnumeratePhysicalDevices (fill)", fn.vkEnumeratePhysicalDevices(instance, &device_count, devices.data()));
    if (device_count > 1) {
        std::uint32_t small = 1;
        VkPhysicalDevice one = nullptr;
        Kit::expect(problems, "vkEnumeratePhysicalDevices with room for one of " + std::to_string(device_count),
                    fn.vkEnumeratePhysicalDevices(instance, &small, &one), {VK_INCOMPLETE});
    }
    std::string error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(error);
    if (std::getenv("LSW_SUITES_DEV_ANY") == nullptr) {
        for (const AvAdapter& adapter : adapters) {
            bool found = false;
            for (std::uint32_t i = 0; i < device_count && !found; ++i) {
                VkPhysicalDeviceIDProperties id{};
                id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
                VkPhysicalDeviceProperties2 props{};
                props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                props.pNext = &id;
                fn.vkGetPhysicalDeviceProperties2(devices[i], &props);
                found = id.deviceLUIDValid != VK_FALSE && std::memcmp(id.deviceLUID, &adapter.luid, sizeof(LUID)) == 0;
            }
            if (!found) {
                problems.add("no Vulkan physical device has the LUID of adapter " + adapter.name + " (" + adapter.luid_text() + ")");
            }
        }
    }
    fn.vkDestroyInstance(instance, nullptr);
    return problems.outcome();
}

Outcome InstanceChecks::props_features_consistent(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        VkPhysicalDeviceProperties basic{};
        rig.fn.vkGetPhysicalDeviceProperties(rig.physical, &basic);
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        rig.fn.vkGetPhysicalDeviceProperties2(rig.physical, &props2);
        const VkPhysicalDeviceProperties& other = props2.properties;
        const auto differ = [&](bool same, const char* field) {
            if (!same) {
                problems.add(std::string("vkGetPhysicalDeviceProperties and vkGetPhysicalDeviceProperties2 disagree on ") + field);
            }
        };
        differ(basic.apiVersion == other.apiVersion, "apiVersion");
        differ(basic.driverVersion == other.driverVersion, "driverVersion");
        differ(basic.vendorID == other.vendorID && basic.deviceID == other.deviceID, "vendorID/deviceID");
        differ(basic.deviceType == other.deviceType, "deviceType");
        differ(std::strcmp(basic.deviceName, other.deviceName) == 0, "deviceName");
        differ(std::memcmp(basic.pipelineCacheUUID, other.pipelineCacheUUID, VK_UUID_SIZE) == 0, "pipelineCacheUUID");
        differ(std::memcmp(&basic.sparseProperties, &other.sparseProperties, sizeof(basic.sparseProperties)) == 0, "sparseProperties");
        differ(limits_equal(basic.limits, other.limits), "limits");
        if (VK_API_VERSION_MAJOR(basic.apiVersion) != 1) {
            problems.add("device apiVersion " + VulkanTools::version_text(basic.apiVersion) + " has a major version other than 1");
        }
        if (std::memchr(basic.deviceName, 0, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE) == nullptr) {
            problems.add("deviceName is not NUL terminated");
        }
        if (rig.id.deviceLUIDValid == VK_FALSE) {
            problems.add("deviceLUIDValid is false on Windows");
        }
        VkPhysicalDeviceIDProperties again{};
        again.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 props_again{};
        props_again.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props_again.pNext = &again;
        rig.fn.vkGetPhysicalDeviceProperties2(rig.physical, &props_again);
        if (std::memcmp(again.deviceUUID, rig.id.deviceUUID, VK_UUID_SIZE) != 0 ||
            std::memcmp(again.driverUUID, rig.id.driverUUID, VK_UUID_SIZE) != 0 ||
            std::memcmp(again.deviceLUID, rig.id.deviceLUID, VK_LUID_SIZE) != 0) {
            problems.add("device and driver UUIDs and the LUID change between two queries");
        }
        if (rig.at_least(VK_API_VERSION_1_2)) {
            VkPhysicalDeviceVulkan11Properties v11{};
            v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
            VkPhysicalDeviceProperties2 chained{};
            chained.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            chained.pNext = &v11;
            rig.fn.vkGetPhysicalDeviceProperties2(rig.physical, &chained);
            if (std::memcmp(v11.deviceUUID, rig.id.deviceUUID, VK_UUID_SIZE) != 0 ||
                std::memcmp(v11.driverUUID, rig.id.driverUUID, VK_UUID_SIZE) != 0 ||
                std::memcmp(v11.deviceLUID, rig.id.deviceLUID, VK_LUID_SIZE) != 0 || v11.deviceLUIDValid != rig.id.deviceLUIDValid ||
                v11.deviceNodeMask != rig.id.deviceNodeMask) {
                problems.add("VkPhysicalDeviceVulkan11Properties disagrees with VkPhysicalDeviceIDProperties");
            }
            if (v11.subgroupSize != rig.subgroup.subgroupSize || v11.subgroupSupportedStages != rig.subgroup.supportedStages ||
                v11.subgroupSupportedOperations != rig.subgroup.supportedOperations ||
                v11.subgroupQuadOperationsInAllStages != rig.subgroup.quadOperationsInAllStages) {
                problems.add("VkPhysicalDeviceVulkan11Properties subgroup fields disagree with VkPhysicalDeviceSubgroupProperties");
            }
            if (v11.maxMemoryAllocationSize != rig.maintenance3.maxMemoryAllocationSize ||
                v11.maxPerSetDescriptors != rig.maintenance3.maxPerSetDescriptors) {
                problems.add("VkPhysicalDeviceVulkan11Properties maintenance3 fields disagree with VkPhysicalDeviceMaintenance3Properties");
            }
        }
        VkPhysicalDeviceFeatures basic_features{};
        rig.fn.vkGetPhysicalDeviceFeatures(rig.physical, &basic_features);
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        rig.fn.vkGetPhysicalDeviceFeatures2(rig.physical, &features2);
        if (!bytes_equal(basic_features, features2.features)) {
            problems.add("vkGetPhysicalDeviceFeatures and vkGetPhysicalDeviceFeatures2 disagree");
        }
        if (rig.at_least(VK_API_VERSION_1_2) && rig.timeline) {
            VkPhysicalDeviceVulkan12Features v12{};
            v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            VkPhysicalDeviceFeatures2 chained{};
            chained.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            chained.pNext = &v12;
            rig.fn.vkGetPhysicalDeviceFeatures2(rig.physical, &chained);
            if (v12.timelineSemaphore == VK_FALSE) {
                problems.add("VkPhysicalDeviceTimelineSemaphoreFeatures reports true and VkPhysicalDeviceVulkan12Features reports false");
            }
        }
        VkPhysicalDeviceMemoryProperties memory{};
        rig.fn.vkGetPhysicalDeviceMemoryProperties(rig.physical, &memory);
        VkPhysicalDeviceMemoryProperties2 memory2{};
        memory2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        rig.fn.vkGetPhysicalDeviceMemoryProperties2(rig.physical, &memory2);
        bool memory_same = memory.memoryTypeCount == memory2.memoryProperties.memoryTypeCount &&
                           memory.memoryHeapCount == memory2.memoryProperties.memoryHeapCount;
        for (std::uint32_t i = 0; memory_same && i < memory.memoryTypeCount; ++i) {
            memory_same = memory.memoryTypes[i].propertyFlags == memory2.memoryProperties.memoryTypes[i].propertyFlags &&
                          memory.memoryTypes[i].heapIndex == memory2.memoryProperties.memoryTypes[i].heapIndex;
        }
        for (std::uint32_t i = 0; memory_same && i < memory.memoryHeapCount; ++i) {
            memory_same = memory.memoryHeaps[i].size == memory2.memoryProperties.memoryHeaps[i].size &&
                          memory.memoryHeaps[i].flags == memory2.memoryProperties.memoryHeaps[i].flags;
        }
        if (!memory_same) {
            problems.add("vkGetPhysicalDeviceMemoryProperties and vkGetPhysicalDeviceMemoryProperties2 disagree");
        }
        for (const VkFormat format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_R32_SFLOAT}) {
            VkFormatProperties plain{};
            rig.fn.vkGetPhysicalDeviceFormatProperties(rig.physical, format, &plain);
            VkFormatProperties2 chained{};
            chained.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
            rig.fn.vkGetPhysicalDeviceFormatProperties2(rig.physical, format, &chained);
            if (!bytes_equal(plain, chained.formatProperties)) {
                problems.add("format properties of " + VulkanTools::format_name(format) + " differ between the two query forms");
            }
        }
        const std::size_t true_count = static_cast<std::size_t>(
            std::count_if(feature_array(basic_features), feature_array(basic_features) + kFeatureCount,
                          [](VkBool32 value) { return value != VK_FALSE; }));
        context.measure("vk.features_supported", true_count);
        VkDevice device = nullptr;
        for (std::size_t i = 0; i < kFeatureCount; ++i) {
            VkPhysicalDeviceFeatures one{};
            reinterpret_cast<VkBool32*>(&one)[i] = VK_TRUE;
            const bool supported = feature_array(basic_features)[i] != VK_FALSE;
            const VkResult result = create_with(rig, &one, nullptr, device);
            if (supported) {
                Kit::succeeds(problems, std::string("vkCreateDevice enabling the supported feature ") + kFeatureNames[i], result);
            } else {
                Kit::expect(problems, std::string("vkCreateDevice enabling the unsupported feature ") + kFeatureNames[i], result,
                            {VK_ERROR_FEATURE_NOT_PRESENT});
            }
            if (result == VK_SUCCESS) {
                drop_device(rig, device);
            }
        }
        const VkResult bad = create_with(rig, nullptr, "VK_LSW_device_extension_that_does_not_exist", device);
        Kit::expect(problems, "vkCreateDevice with an unsupported extension", bad, {VK_ERROR_EXTENSION_NOT_PRESENT});
        if (bad == VK_SUCCESS) {
            drop_device(rig, device);
        }
        std::uint32_t count = 0;
        Kit::succeeds(problems, "vkEnumerateDeviceExtensionProperties (count)",
                      rig.fn.vkEnumerateDeviceExtensionProperties(rig.physical, nullptr, &count, nullptr));
        if (count > 1) {
            std::uint32_t small = 1;
            VkExtensionProperties one{};
            Kit::expect(problems, "vkEnumerateDeviceExtensionProperties with room for one of " + std::to_string(count),
                        rig.fn.vkEnumerateDeviceExtensionProperties(rig.physical, nullptr, &small, &one), {VK_INCOMPLETE});
        }
        return {};
    });
}

Outcome InstanceChecks::limits_minimums_honoured(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        const VkPhysicalDeviceLimits& l = rig.props.limits;
        const VkPhysicalDeviceFeatures& f = rig.features;
        const auto least = [&](const char* name, std::uint64_t value, std::uint64_t minimum) {
            if (value < minimum) {
                problems.add(std::string(name) + " is " + std::to_string(value) + ", below the required minimum " + std::to_string(minimum));
            }
        };
        const auto most = [&](const char* name, std::uint64_t value, std::uint64_t maximum) {
            if (value > maximum) {
                problems.add(std::string(name) + " is " + std::to_string(value) + ", above the required maximum " + std::to_string(maximum));
            }
        };
        const auto power_of_two = [&](const char* name, std::uint64_t value) {
            if (value == 0 || (value & (value - 1)) != 0) {
                problems.add(std::string(name) + " is " + std::to_string(value) + ", not a power of two");
            }
        };
        least("maxImageDimension1D", l.maxImageDimension1D, 4096);
        least("maxImageDimension2D", l.maxImageDimension2D, 4096);
        least("maxImageDimension3D", l.maxImageDimension3D, 256);
        least("maxImageDimensionCube", l.maxImageDimensionCube, 4096);
        least("maxImageArrayLayers", l.maxImageArrayLayers, 256);
        least("maxTexelBufferElements", l.maxTexelBufferElements, 65536);
        least("maxUniformBufferRange", l.maxUniformBufferRange, 16384);
        least("maxStorageBufferRange", l.maxStorageBufferRange, 1u << 27);
        least("maxPushConstantsSize", l.maxPushConstantsSize, 128);
        least("maxMemoryAllocationCount", l.maxMemoryAllocationCount, 4096);
        least("maxSamplerAllocationCount", l.maxSamplerAllocationCount, 4000);
        most("bufferImageGranularity", l.bufferImageGranularity, 131072);
        least("maxBoundDescriptorSets", l.maxBoundDescriptorSets, 4);
        least("maxPerStageDescriptorSamplers", l.maxPerStageDescriptorSamplers, 16);
        least("maxPerStageDescriptorUniformBuffers", l.maxPerStageDescriptorUniformBuffers, 12);
        least("maxPerStageDescriptorStorageBuffers", l.maxPerStageDescriptorStorageBuffers, 4);
        least("maxPerStageDescriptorSampledImages", l.maxPerStageDescriptorSampledImages, 16);
        least("maxPerStageDescriptorStorageImages", l.maxPerStageDescriptorStorageImages, 4);
        least("maxPerStageDescriptorInputAttachments", l.maxPerStageDescriptorInputAttachments, 4);
        least("maxPerStageResources", l.maxPerStageResources, 128);
        least("maxDescriptorSetSamplers", l.maxDescriptorSetSamplers, 96);
        least("maxDescriptorSetUniformBuffers", l.maxDescriptorSetUniformBuffers, 72);
        least("maxDescriptorSetUniformBuffersDynamic", l.maxDescriptorSetUniformBuffersDynamic, 8);
        least("maxDescriptorSetStorageBuffers", l.maxDescriptorSetStorageBuffers, 24);
        least("maxDescriptorSetStorageBuffersDynamic", l.maxDescriptorSetStorageBuffersDynamic, 4);
        least("maxDescriptorSetSampledImages", l.maxDescriptorSetSampledImages, 96);
        least("maxDescriptorSetStorageImages", l.maxDescriptorSetStorageImages, 24);
        least("maxDescriptorSetInputAttachments", l.maxDescriptorSetInputAttachments, 4);
        least("maxVertexInputAttributes", l.maxVertexInputAttributes, 16);
        least("maxVertexInputBindings", l.maxVertexInputBindings, 16);
        least("maxVertexInputAttributeOffset", l.maxVertexInputAttributeOffset, 2047);
        least("maxVertexInputBindingStride", l.maxVertexInputBindingStride, 2048);
        least("maxVertexOutputComponents", l.maxVertexOutputComponents, 64);
        if (f.tessellationShader != VK_FALSE) {
            least("maxTessellationGenerationLevel", l.maxTessellationGenerationLevel, 64);
            least("maxTessellationPatchSize", l.maxTessellationPatchSize, 32);
        }
        if (f.geometryShader != VK_FALSE) {
            least("maxGeometryShaderInvocations", l.maxGeometryShaderInvocations, 32);
            least("maxGeometryOutputVertices", l.maxGeometryOutputVertices, 256);
            least("maxFramebufferLayers", l.maxFramebufferLayers, 256);
        }
        least("maxFragmentInputComponents", l.maxFragmentInputComponents, 64);
        least("maxFragmentOutputAttachments", l.maxFragmentOutputAttachments, 4);
        if (f.dualSrcBlend != VK_FALSE) {
            least("maxFragmentDualSrcAttachments", l.maxFragmentDualSrcAttachments, 1);
        }
        least("maxFragmentCombinedOutputResources", l.maxFragmentCombinedOutputResources, 4);
        least("maxComputeSharedMemorySize", l.maxComputeSharedMemorySize, 16384);
        for (int i = 0; i < 3; ++i) {
            least("maxComputeWorkGroupCount", l.maxComputeWorkGroupCount[i], 65535);
            least("maxComputeWorkGroupSize", l.maxComputeWorkGroupSize[i], i == 2 ? 64u : 128u);
        }
        least("maxComputeWorkGroupInvocations", l.maxComputeWorkGroupInvocations, 128);
        least("subPixelPrecisionBits", l.subPixelPrecisionBits, 4);
        least("subTexelPrecisionBits", l.subTexelPrecisionBits, 4);
        least("mipmapPrecisionBits", l.mipmapPrecisionBits, 4);
        least("maxDrawIndexedIndexValue", l.maxDrawIndexedIndexValue, (1u << 24) - 1);
        least("maxDrawIndirectCount", l.maxDrawIndirectCount, f.multiDrawIndirect != VK_FALSE ? 65535u : 1u);
        if (l.maxSamplerLodBias < 2.0f) {
            problems.add("maxSamplerLodBias is below 2.0");
        }
        if (f.samplerAnisotropy != VK_FALSE) {
            if (l.maxSamplerAnisotropy < 16.0f) {
                problems.add("maxSamplerAnisotropy is below 16.0");
            }
        }
        least("maxViewports", l.maxViewports, f.multiViewport != VK_FALSE ? 16u : 1u);
        least("maxViewportDimensions[0]", l.maxViewportDimensions[0], 4096);
        least("maxViewportDimensions[1]", l.maxViewportDimensions[1], 4096);
        if (l.viewportBoundsRange[0] > -2.0f * static_cast<float>(l.maxViewportDimensions[0]) + 1.0f ||
            l.viewportBoundsRange[1] < 2.0f * static_cast<float>(l.maxViewportDimensions[0]) - 1.0f) {
            problems.add("viewportBoundsRange does not cover twice maxViewportDimensions");
        }
        least("minMemoryMapAlignment", l.minMemoryMapAlignment, 64);
        power_of_two("minMemoryMapAlignment", l.minMemoryMapAlignment);
        most("minTexelBufferOffsetAlignment", l.minTexelBufferOffsetAlignment, 256);
        most("minUniformBufferOffsetAlignment", l.minUniformBufferOffsetAlignment, 256);
        most("minStorageBufferOffsetAlignment", l.minStorageBufferOffsetAlignment, 256);
        power_of_two("minTexelBufferOffsetAlignment", l.minTexelBufferOffsetAlignment);
        power_of_two("minUniformBufferOffsetAlignment", l.minUniformBufferOffsetAlignment);
        power_of_two("minStorageBufferOffsetAlignment", l.minStorageBufferOffsetAlignment);
        power_of_two("nonCoherentAtomSize", l.nonCoherentAtomSize);
        most("nonCoherentAtomSize", l.nonCoherentAtomSize, 256);
        if (l.minTexelOffset > -8 || l.maxTexelOffset < 7) {
            problems.add("minTexelOffset/maxTexelOffset do not cover -8..7");
        }
        if (f.shaderImageGatherExtended != VK_FALSE && (l.minTexelGatherOffset > -8 || l.maxTexelGatherOffset < 7)) {
            problems.add("minTexelGatherOffset/maxTexelGatherOffset do not cover -8..7");
        }
        if (f.sampleRateShading != VK_FALSE && (l.minInterpolationOffset > -0.5f || l.maxInterpolationOffset < 0.4375f)) {
            problems.add("interpolation offset range does not cover -0.5..0.4375");
        }
        least("maxFramebufferWidth", l.maxFramebufferWidth, 4096);
        least("maxFramebufferHeight", l.maxFramebufferHeight, 4096);
        const VkSampleCountFlags required = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
        if ((l.framebufferColorSampleCounts & required) != required || (l.framebufferDepthSampleCounts & required) != required ||
            (l.sampledImageColorSampleCounts & required) != required || (l.sampledImageIntegerSampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0 ||
            (l.sampledImageDepthSampleCounts & required) != required || (l.storageImageSampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0) {
            problems.add("the sample count masks do not include the required 1 and 4 samples");
        }
        least("maxColorAttachments", l.maxColorAttachments, 4);
        least("maxSampleMaskWords", l.maxSampleMaskWords, 1);
        least("maxClipDistances", l.maxClipDistances, f.shaderClipDistance != VK_FALSE ? 8u : 0u);
        least("maxCullDistances", l.maxCullDistances, f.shaderCullDistance != VK_FALSE ? 8u : 0u);
        least("discreteQueuePriorities", l.discreteQueuePriorities, 2);
        if (l.pointSizeRange[0] > 1.0f || l.pointSizeRange[1] < 1.0f || (f.largePoints != VK_FALSE && l.pointSizeRange[1] < 64.0f - l.pointSizeGranularity)) {
            problems.add("pointSizeRange does not satisfy the required bounds");
        }
        if (l.lineWidthRange[0] > 1.0f || l.lineWidthRange[1] < 1.0f || (f.wideLines != VK_FALSE && l.lineWidthRange[1] < 8.0f - l.lineWidthGranularity)) {
            problems.add("lineWidthRange does not satisfy the required bounds");
        }
        if (l.timestampPeriod <= 0.0f) {
            problems.add("timestampPeriod is not positive");
        }
        context.measure("vk.timestampPeriod_ns", static_cast<double>(l.timestampPeriod));
        context.measure("vk.maxPushConstantsSize", l.maxPushConstantsSize);
        context.measure("vk.maxComputeWorkGroupInvocations", l.maxComputeWorkGroupInvocations);
        if (!problems.empty()) {
            return {};
        }
        const std::uint32_t push_words = std::min<std::uint32_t>(l.maxPushConstantsSize / 4, 1024);
        const std::uint32_t group = std::min({l.maxComputeWorkGroupSize[0], l.maxComputeWorkGroupInvocations, 1024u});
        const std::uint32_t groups = 3;
        const std::uint32_t spec_values[2] = {group, push_words};
        const VkSpecializationMapEntry entries[2] = {{0, 0, 4}, {1, 4, 4}};
        const VkSpecializationInfo specialization{2, entries, sizeof(spec_values), spec_values};
        VulkanCompute limits;
        VkResult result = limits.create(rig, VulkanShaders::words(VulkanShader::Limits), {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}},
                                        push_words * 4, &specialization);
        VulkanBuffer output;
        if (Kit::succeeds(problems, "compute pipeline with a full push constant block and a workgroup of " + std::to_string(group), result) &&
            Kit::succeeds(problems, "output buffer", Kit::host_buffer(rig, output, static_cast<VkDeviceSize>(group) * groups * 4))) {
            output.fill_value(0xCD);
            output.publish();
            limits.write_buffer(0, output.buffer);
            std::vector<std::uint32_t> push(push_words);
            std::uint32_t acc = 0;
            for (std::uint32_t k = 0; k < push_words; ++k) {
                push[k] = k * 7 + 1;
                acc += push[k];
            }
            if (Kit::succeeds(problems, "dispatch at the push constant and workgroup limits",
                              limits.run(push.data(), push_words * 4, groups))) {
                output.acquire();
                std::vector<std::uint32_t> expected(static_cast<std::size_t>(group) * groups);
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    expected[i] = ((static_cast<std::uint32_t>(i) % group) | ((static_cast<std::uint32_t>(i) / group) << 16)) ^ acc;
                }
                const std::vector<std::uint32_t> actual(output.words(), output.words() + expected.size());
                Kit::same_words(context, problems, "limits shader output", expected, actual);
            }
        }
        const bool descriptor_room = l.maxPerStageDescriptorStorageBuffers >= 8 && l.maxDescriptorSetStorageBuffers >= 8;
        if (descriptor_room) {
            VulkanCompute bindings;
            result = bindings.create(rig, VulkanShaders::words(VulkanShader::Bindings), {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}}, 0);
            std::vector<VulkanBuffer> buffers(8);
            bool ready = Kit::succeeds(problems, "pipeline with a storage buffer array of 8", result);
            for (std::uint32_t k = 0; ready && k < 8; ++k) {
                ready = Kit::succeeds(problems, "descriptor buffer", Kit::host_buffer(rig, buffers[k], 16));
                if (ready) {
                    buffers[k].words()[0] = k + 1;
                    buffers[k].publish();
                    bindings.write_buffer(0, buffers[k].buffer, 0, 16, k);
                }
            }
            if (ready && Kit::succeeds(problems, "dispatch over 8 bound storage buffers", bindings.run(nullptr, 0, 1))) {
                for (std::uint32_t k = 0; k < 8; ++k) {
                    buffers[k].acquire();
                    if (buffers[k].words()[0] != 36 + k) {
                        problems.add("storage buffer " + std::to_string(k) + " holds " + std::to_string(buffers[k].words()[0]) +
                                     ", expected " + std::to_string(36 + k));
                    }
                }
            }
        }
        VkImageFormatProperties format{};
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (Kit::image_supported(rig, VK_FORMAT_R8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, format)) {
            if (format.maxExtent.width > l.maxImageDimension2D || format.maxExtent.height > l.maxImageDimension2D) {
                problems.add("R8_UNORM 2D maxExtent exceeds maxImageDimension2D");
            }
            const std::uint32_t wide = std::min<std::uint32_t>(format.maxExtent.width, 32768);
            const std::uint32_t tall = std::min<std::uint32_t>(format.maxExtent.height, 32768);
            for (const bool horizontal : {true, false}) {
                VulkanImage image;
                const std::uint32_t w = horizontal ? wide : 1;
                const std::uint32_t h = horizontal ? 1 : tall;
                const std::string what = "R8_UNORM " + std::to_string(w) + "x" + std::to_string(h) + " image";
                if (!Kit::succeeds(problems, "create " + what, image.create_2d(rig, VK_FORMAT_R8_UNORM, w, h, usage)) ||
                    !Kit::succeeds(problems, "transition " + what, VulkanImageIo::to_general(rig, image))) {
                    continue;
                }
                std::vector<std::uint8_t> data(static_cast<std::size_t>(w) * h);
                Pattern::fill(0x1234 + (horizontal ? 1 : 2), 0, data.data(), data.size());
                ImageRegion region;
                region.extent = {w, h, 1};
                std::vector<std::uint8_t> back;
                if (Kit::succeeds(problems, "write " + what, VulkanImageIo::write(rig, image, region, data)) &&
                    Kit::succeeds(problems, "read " + what, VulkanImageIo::read(rig, image, region, back, 1))) {
                    Kit::same_bytes(context, problems, what + " roundtrip", data.data(), back.data(), data.size());
                }
            }
        }
        const std::uint32_t view_w = std::min<std::uint32_t>(l.maxViewportDimensions[0], 32768);
        const std::uint32_t view_h = std::min<std::uint32_t>(l.maxViewportDimensions[1], 32768);
        SceneOptions options;
        options.width = 16;
        options.height = 16;
        options.viewport_width = view_w;
        options.viewport_height = view_h;
        VulkanScene scene;
        if (Kit::succeeds(problems, "scene with a " + std::to_string(view_w) + "x" + std::to_string(view_h) + " viewport",
                          scene.create(rig, options))) {
            SceneRect rect;
            rect.x1 = static_cast<float>(view_w);
            rect.y1 = static_cast<float>(view_h);
            rect.color[0] = 0.0f;
            rect.color[1] = 1.0f;
            rect.color[2] = 0.0f;
            const float clear[4] = {1.0f, 0.0f, 0.0f, 1.0f};
            SceneResult out;
            if (Kit::succeeds(problems, "render at the viewport limit", scene.render({rect}, clear, 1.0f, out))) {
                const std::vector<std::uint8_t> expected = scene.expected_color({rect}, clear, 1.0f, false);
                Kit::same_bytes(context, problems, "viewport limit render", expected.data(), out.color.data(), expected.size());
            }
        }
        return {};
    });
}

Outcome InstanceChecks::queue_families(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::uint32_t count = 0;
        rig.fn.vkGetPhysicalDeviceQueueFamilyProperties2(rig.physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties2> second(count);
        for (VkQueueFamilyProperties2& item : second) {
            item.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        }
        rig.fn.vkGetPhysicalDeviceQueueFamilyProperties2(rig.physical, &count, second.data());
        if (count != rig.families.size()) {
            problems.add("vkGetPhysicalDeviceQueueFamilyProperties2 reports " + std::to_string(count) + " families, the plain query " +
                         std::to_string(rig.families.size()));
        }
        bool graphics_compute = false;
        for (std::size_t i = 0; i < rig.families.size(); ++i) {
            const VkQueueFamilyProperties& family = rig.families[i];
            const std::string label = "queue family " + std::to_string(i);
            if (i < second.size() && std::memcmp(&family, &second[i].queueFamilyProperties, sizeof(family)) != 0) {
                problems.add(label + " differs between the two query forms");
            }
            if (family.queueCount == 0) {
                problems.add(label + " has no queues");
            }
            if (family.queueFlags == 0) {
                problems.add(label + " has no capability flags");
            }
            if (family.timestampValidBits != 0 && (family.timestampValidBits < 36 || family.timestampValidBits > 64)) {
                problems.add(label + " has timestampValidBits " + std::to_string(family.timestampValidBits) + ", outside 36..64 and not 0");
            }
            const VkExtent3D granularity = family.minImageTransferGranularity;
            if (granularity.width == 0 || granularity.height == 0 || granularity.depth == 0) {
                problems.add(label + " has a zero minImageTransferGranularity component");
            }
            graphics_compute = graphics_compute || ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0);
            if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && (family.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) {
                problems.add(label + " supports graphics without compute although the specification requires both together");
            }
        }
        if (!graphics_compute) {
            problems.add("no queue family supports graphics and compute together");
        }
        VulkanCompute ops;
        const bool any_compute = std::any_of(rig.queues.begin(), rig.queues.end(), [](const VulkanQueue& q) {
            return (q.properties.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        });
        if (any_compute) {
            Kit::succeeds(problems, "ops pipeline", ops.create(rig, VulkanShaders::words(VulkanShader::Ops),
                                                                  {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                                                   {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                                                   {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}},
                                                                  8));
        }
        for (VulkanQueue* family_queue : rig.queues_supporting(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) {
            VulkanQueue& queue = *family_queue;
            const std::string label = "queue " + std::to_string(queue.index) + " of family " + std::to_string(queue.family);
            const VkQueueFlags flags = queue.properties.queueFlags;
            VulkanBuffer a;
            VulkanBuffer b;
            if (!Kit::succeeds(problems, label + " source buffer", Kit::host_buffer(rig, a, 256)) ||
                !Kit::succeeds(problems, label + " destination buffer", Kit::host_buffer(rig, b, 256))) {
                continue;
            }
            const std::vector<std::uint32_t> data = Kit::words(0x51 + queue.family * 8 + queue.index, 64);
            b.fill_value(0xCD);
            b.publish();
            const VkResult result = rig.run(
                [&](VkCommandBuffer commands) {
                    rig.fn.vkCmdUpdateBuffer(commands, a.buffer, 0, 256, data.data());
                    VkMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier,
                                                0, nullptr, 0, nullptr);
                    const VkBufferCopy region{0, 0, 256};
                    rig.fn.vkCmdCopyBuffer(commands, a.buffer, b.buffer, 1, &region);
                    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                                                nullptr, 0, nullptr);
                },
                &queue);
            if (Kit::succeeds(problems, label + " transfer submission", result)) {
                b.acquire();
                Kit::same_bytes(context, problems, label + " transfer result", data.data(), b.bytes(), 256);
            }
            if ((flags & VK_QUEUE_COMPUTE_BIT) != 0 && ops.set != VK_NULL_HANDLE) {
                VulkanBuffer uniform;
                VulkanBuffer output;
                if (!Kit::succeeds(problems, label + " uniform buffer", Kit::host_buffer(rig, uniform, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) ||
                    !Kit::succeeds(problems, label + " output buffer", Kit::host_buffer(rig, output, 256))) {
                    continue;
                }
                uniform.words()[0] = 3;
                uniform.words()[1] = 5;
                uniform.words()[2] = 64;
                uniform.words()[3] = 0;
                uniform.publish();
                ops.write_buffer(0, a.buffer);
                ops.write_buffer(1, output.buffer);
                ops.write_buffer(2, uniform.buffer);
                const std::uint32_t push[2] = {0, 9};
                if (Kit::succeeds(problems, label + " compute submission", ops.run(push, sizeof(push), 1, 1, 1, &queue))) {
                    output.acquire();
                    std::vector<std::uint32_t> expected(64);
                    for (std::size_t i = 0; i < 64; ++i) {
                        expected[i] = data[i] * 3 + 5 + 9;
                    }
                    const std::vector<std::uint32_t> actual(output.words(), output.words() + 64);
                    Kit::same_words(context, problems, label + " compute result", expected, actual);
                }
            }
        }
        context.measure("vk.queue_families", rig.families.size());
        context.measure("vk.queues_exercised", rig.queues.size());
        return {};
    });
}

Outcome InstanceChecks::extensions_match_functions(Context& context) {
    return Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        std::vector<std::string> names(rig.device_extensions.begin(), rig.device_extensions.end());
        struct Generic {
            VkStructureType sType;
            void* pNext;
            VkBool32 values[64];
        };
        std::vector<Generic> generic;
        for (const VulkanExtensionFeature& feature : VulkanExtensionTable::feature_structs()) {
            if (rig.device_has(feature.extension)) {
                Generic entry{};
                entry.sType = feature.type;
                generic.push_back(entry);
            }
        }
        void* head = nullptr;
        for (Generic& entry : generic) {
            entry.pNext = head;
            head = &entry;
        }
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = head;
        rig.fn.vkGetPhysicalDeviceFeatures2(rig.physical, &features2);
        context.measure("vk.feature_structs_chained", generic.size());
        std::vector<const char*> pointers;
        for (const std::string& name : names) {
            pointers.push_back(name.c_str());
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = rig.main().family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures2 none{};
        none.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.pNext = &none;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = static_cast<std::uint32_t>(pointers.size());
        info.ppEnabledExtensionNames = pointers.data();
        VkDevice device = nullptr;
        const VkResult created = rig.fn.vkCreateDevice(rig.physical, &info, nullptr, &device);
        if (!Kit::succeeds(problems, "vkCreateDevice with all " + std::to_string(names.size()) + " advertised extensions", created)) {
            return {};
        }
        std::size_t resolved = 0;
        std::size_t known = 0;
        for (const std::string& name : names) {
            const std::vector<std::string> functions = VulkanExtensionTable::device_functions(name);
            known += functions.empty() ? std::size_t{0} : std::size_t{1};
            for (const std::string& function : functions) {
                if (rig.fn.vkGetDeviceProcAddr(device, function.c_str()) == nullptr) {
                    problems.add(name + " is advertised but " + function + " does not resolve through vkGetDeviceProcAddr");
                } else {
                    ++resolved;
                }
            }
        }
        context.measure("vk.device_extensions", names.size());
        context.measure("vk.device_extensions_with_known_functions", known);
        context.measure("vk.device_functions_resolved", resolved);
        drop_device(rig, device);
        return {};
    });
}

}  // namespace lsw::suites::sanity::vk
