#include "render/vk/kit.hpp"

#include "render/vk/presenter.hpp"
#include "render/vk/shader_registry.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace lsw::render::vk {

Kit::~Kit() { close(); }

void Kit::close() {
    if (device != VK_NULL_HANDLE && api.vkDeviceWaitIdle != nullptr) {
        api.vkDeviceWaitIdle(device);
    }
    if (presenter) {
        presenter->close(*this);
        presenter.reset();
    }
    for (auto it = cleanups_.rbegin(); it != cleanups_.rend(); ++it) {
        (*it)();
    }
    cleanups_.clear();
    if (device != VK_NULL_HANDLE) {
        if (fence_ != VK_NULL_HANDLE) {
            api.vkDestroyFence(device, fence_, nullptr);
        }
        if (pool != VK_NULL_HANDLE) {
            api.vkDestroyCommandPool(device, pool, nullptr);
        }
        api.vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE && api.vkDestroyInstance != nullptr) {
        api.vkDestroyInstance(instance, nullptr);
    }
    fence_ = VK_NULL_HANDLE;
    pool = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
}

bool Kit::check(VkResult result, const char* what, std::string& error) const {
    if (result == VK_SUCCESS) {
        return true;
    }
    error = std::string(what) + " failed with " + Api::result_name(result);
    return false;
}

bool Kit::open(platform::VkPlatform& host, const KitOptions& options, std::string& error) {
    platform = &host;
    present_enabled = options.present;
    PFN_vkGetInstanceProcAddr loader = host.loader(error);
    if (loader == nullptr || !api.load_global(loader, error)) {
        return false;
    }
    std::uint32_t instance_version = VK_API_VERSION_1_0;
    if (api.vkEnumerateInstanceVersion != nullptr) {
        api.vkEnumerateInstanceVersion(&instance_version);
    }
    if (instance_version < options.api_version) {
        error = "the Vulkan loader reports an instance version below the one the case needs";
        return false;
    }
    std::uint32_t count = 0;
    api.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    api.vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
    std::set<std::string> have;
    for (const VkExtensionProperties& p : available) {
        have.insert(p.extensionName);
    }
    std::vector<std::string> wanted = host.instance_extensions(options.present);
    for (const std::string& name : wanted) {
        if (have.count(name) == 0) {
            error = "instance extension " + name + " is not available";
            return false;
        }
    }
    std::vector<const char*> names;
    for (const std::string& name : wanted) {
        names.push_back(name.c_str());
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "lsw-render";
    app.apiVersion = options.api_version;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(names.size());
    info.ppEnabledExtensionNames = names.data();
    if (!check(api.vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance", error) || !api.load_instance(instance, error)) {
        return false;
    }
    count = 0;
    api.vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    api.vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (devices.empty()) {
        error = "no Vulkan physical device";
        return false;
    }
    std::string seen;
    for (std::uint32_t i = 0; i < devices.size() && physical == VK_NULL_HANDLE; ++i) {
        VkPhysicalDeviceIDProperties id{};
        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &id;
        api.vkGetPhysicalDeviceProperties2(devices[i], &props2);
        platform::VkDeviceIdentity candidate;
        candidate.index = i;
        std::memcpy(candidate.uuid, id.deviceUUID, 16);
        std::memcpy(candidate.luid, id.deviceLUID, 8);
        candidate.luid_valid = id.deviceLUIDValid != VK_FALSE;
        candidate.vendor = props2.properties.vendorID;
        candidate.device = props2.properties.deviceID;
        candidate.name = props2.properties.deviceName;
        seen += (seen.empty() ? "" : "; ") + candidate.name;
        if (host.accepts(candidate)) {
            physical = devices[i];
            identity = candidate;
            properties = props2.properties;
        }
    }
    if (physical == VK_NULL_HANDLE) {
        error = "no Vulkan physical device matches the requested GPU (saw: " + seen + ")";
        return false;
    }
    if (properties.apiVersion < options.api_version) {
        error = "the device reports a Vulkan version below the one the case needs";
        return false;
    }
    api.vkGetPhysicalDeviceFeatures(physical, &features);
    api.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    count = 0;
    api.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    api.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    bool found = false;
    for (std::uint32_t i = 0; i < count && !found; ++i) {
        if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            family = i;
            found = true;
        }
    }
    if (!found) {
        error = "no graphics and compute queue family";
        return false;
    }
    count = 0;
    api.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> device_available(count);
    api.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, device_available.data());
    std::set<std::string> device_have;
    for (const VkExtensionProperties& p : device_available) {
        device_have.insert(p.extensionName);
    }
    std::vector<std::string> device_wanted = host.device_extensions(options.present);
    device_wanted.insert(device_wanted.end(), options.device_extensions.begin(), options.device_extensions.end());
    for (const std::string& name : device_wanted) {
        if (device_have.count(name) == 0) {
            error = "device extension " + name + " is not available";
            return false;
        }
    }
    std::vector<const char*> device_names;
    for (const std::string& name : device_wanted) {
        device_names.push_back(name.c_str());
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures enabled = features;
    enabled.robustBufferAccess = VK_FALSE;
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_names.size());
    device_info.ppEnabledExtensionNames = device_names.data();
    device_info.pEnabledFeatures = &enabled;
    if (!check(api.vkCreateDevice(physical, &device_info, nullptr, &device), "vkCreateDevice", error) ||
        !api.load_device(device, error)) {
        return false;
    }
    api.vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = family;
    if (!check(api.vkCreateCommandPool(device, &pool_info, nullptr, &pool), "vkCreateCommandPool", error)) {
        return false;
    }
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (!check(api.vkAllocateCommandBuffers(device, &alloc, &command_), "vkAllocateCommandBuffers", error) ||
        !check(api.vkCreateFence(device, &fence_info, nullptr, &fence_), "vkCreateFence", error)) {
        return false;
    }
    description = std::string(properties.deviceName) + " vendor 0x" + [&] {
        char text[16];
        std::snprintf(text, sizeof text, "%04x", properties.vendorID);
        return std::string(text);
    }();
    if (options.present) {
        presenter = host.make_presenter(error);
        if (!presenter) {
            return false;
        }
    }
    return true;
}

bool Kit::format_supports(VkFormat format, VkFormatFeatureFlags optimal) const {
    VkFormatProperties props{};
    api.vkGetPhysicalDeviceFormatProperties(physical, format, &props);
    return (props.optimalTilingFeatures & optimal) == optimal;
}

VkFormat Kit::first_supported(const std::vector<VkFormat>& candidates, VkFormatFeatureFlags optimal) const {
    for (VkFormat format : candidates) {
        if (format_supports(format, optimal)) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

std::uint32_t Kit::memory_type(std::uint32_t bits, VkMemoryPropertyFlags required) const {
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0 && (memory.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    return ~0u;
}

VkShaderModule Kit::shader(const std::string& name, std::string& error) {
    std::vector<std::uint32_t> words;
    if (!ShaderRegistry::find(name, words)) {
        error = "no SPIR-V registered under " + name;
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * 4;
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (!check(api.vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule", error)) {
        return VK_NULL_HANDLE;
    }
    defer([this, module] { api.vkDestroyShaderModule(device, module, nullptr); });
    return module;
}

bool Kit::make_buffer(Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible, std::string& error) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    if (!check(api.vkCreateBuffer(device, &info, nullptr, &buffer), "vkCreateBuffer", error)) {
        return false;
    }
    defer([this, buffer] { api.vkDestroyBuffer(device, buffer, nullptr); });
    VkMemoryRequirements req{};
    api.vkGetBufferMemoryRequirements(device, buffer, &req);
    const VkMemoryPropertyFlags flags = host_visible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                                     : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    std::uint32_t type = memory_type(req.memoryTypeBits, flags);
    if (type == ~0u && !host_visible) {
        type = memory_type(req.memoryTypeBits, 0);
    }
    if (type == ~0u) {
        error = "no memory type for a buffer";
        return false;
    }
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (!check(api.vkAllocateMemory(device, &alloc, nullptr, &mem), "vkAllocateMemory", error)) {
        return false;
    }
    defer([this, mem] { api.vkFreeMemory(device, mem, nullptr); });
    if (!check(api.vkBindBufferMemory(device, buffer, mem, 0), "vkBindBufferMemory", error)) {
        return false;
    }
    void* mapped = nullptr;
    if (host_visible && !check(api.vkMapMemory(device, mem, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory", error)) {
        return false;
    }
    out.buffer = buffer;
    out.memory = mem;
    out.mapped = mapped;
    out.size = size;
    return true;
}

bool Kit::make_buffer_with(Buffer& out, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, std::string& error) {
    if (!make_buffer(out, size, usage, true, error)) {
        return false;
    }
    return write(out, 0, data, size, error);
}

bool Kit::write(Buffer& buffer, VkDeviceSize offset, const void* data, VkDeviceSize size, std::string& error) {
    if (buffer.mapped == nullptr || offset + size > buffer.size) {
        error = "buffer write out of range or unmapped";
        return false;
    }
    std::memcpy(static_cast<std::uint8_t*>(buffer.mapped) + offset, data, static_cast<std::size_t>(size));
    return true;
}

bool Kit::make_texture(Texture& out, const TextureDesc& desc, std::string& error) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.flags = desc.view_type == VK_IMAGE_VIEW_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = desc.mips;
    info.arrayLayers = desc.layers;
    info.samples = desc.samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    if (!check(api.vkCreateImage(device, &info, nullptr, &image), "vkCreateImage", error)) {
        return false;
    }
    defer([this, image] { api.vkDestroyImage(device, image, nullptr); });
    VkMemoryRequirements req{};
    api.vkGetImageMemoryRequirements(device, image, &req);
    std::uint32_t type = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == ~0u) {
        type = memory_type(req.memoryTypeBits, 0);
    }
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (type == ~0u || !check(api.vkAllocateMemory(device, &alloc, nullptr, &mem), "vkAllocateMemory", error)) {
        if (type == ~0u) {
            error = "no memory type for an image";
        }
        return false;
    }
    defer([this, mem] { api.vkFreeMemory(device, mem, nullptr); });
    if (!check(api.vkBindImageMemory(device, image, mem, 0), "vkBindImageMemory", error)) {
        return false;
    }
    VkImageView view = VK_NULL_HANDLE;
    const bool viewable = (desc.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                         VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) != 0;
    if (viewable) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = desc.view_type;
        view_info.format = desc.format;
        view_info.subresourceRange = {desc.aspect, 0, desc.mips, 0, desc.layers};
        if (!check(api.vkCreateImageView(device, &view_info, nullptr, &view), "vkCreateImageView", error)) {
            return false;
        }
        defer([this, view] { api.vkDestroyImageView(device, view, nullptr); });
    }
    out.image = image;
    out.memory = mem;
    out.view = view;
    out.format = desc.format;
    out.width = desc.width;
    out.height = desc.height;
    out.layers = desc.layers;
    out.mips = desc.mips;
    out.samples = desc.samples;
    out.aspect = desc.aspect;
    return true;
}

bool Kit::make_sampled_texture(Texture& out, std::uint32_t width, std::uint32_t height, VkFormat format,
                               const std::vector<std::vector<std::uint8_t>>& levels, std::string& error) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.mips = static_cast<std::uint32_t>(levels.size());
    desc.format = format;
    if (!make_texture(out, desc, error)) {
        return false;
    }
    std::vector<std::uint8_t> all;
    std::vector<VkBufferImageCopy> regions;
    std::uint32_t w = width;
    std::uint32_t h = height;
    for (std::uint32_t level = 0; level < levels.size(); ++level) {
        VkBufferImageCopy region{};
        region.bufferOffset = all.size();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        region.imageExtent = {w, h, 1};
        regions.push_back(region);
        all.insert(all.end(), levels[level].begin(), levels[level].end());
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    Buffer staging;
    if (!make_buffer_with(staging, all.data(), all.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, error)) {
        return false;
    }
    return submit(
        [&](VkCommandBuffer cb) {
            barrier(cb, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, out.mips);
            api.vkCmdCopyBufferToImage(cb, staging.buffer, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       static_cast<std::uint32_t>(regions.size()), regions.data());
            barrier(cb, out.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, out.mips);
        },
        error);
}

bool Kit::make_sampler(VkSampler& out, VkFilter mag, VkFilter min, VkSamplerMipmapMode mip, VkSamplerAddressMode address,
                       float max_lod, std::string& error, float anisotropy) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = mag;
    info.minFilter = min;
    info.mipmapMode = mip;
    info.addressModeU = address;
    info.addressModeV = address;
    info.addressModeW = address;
    info.maxLod = max_lod;
    info.anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = anisotropy > 1.0f ? anisotropy : 1.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (!check(api.vkCreateSampler(device, &info, nullptr, &out), "vkCreateSampler", error)) {
        return false;
    }
    const VkSampler sampler = out;
    defer([this, sampler] { api.vkDestroySampler(device, sampler, nullptr); });
    return true;
}

bool Kit::make_layout(VkDescriptorSetLayout& out, const std::vector<VkDescriptorSetLayoutBinding>& bindings, std::string& error) {
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    if (!check(api.vkCreateDescriptorSetLayout(device, &info, nullptr, &out), "vkCreateDescriptorSetLayout", error)) {
        return false;
    }
    const VkDescriptorSetLayout layout = out;
    defer([this, layout] { api.vkDestroyDescriptorSetLayout(device, layout, nullptr); });
    return true;
}

bool Kit::make_pipeline_layout(VkPipelineLayout& out, const std::vector<VkDescriptorSetLayout>& sets,
                               const std::vector<VkPushConstantRange>& push, std::string& error) {
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<std::uint32_t>(sets.size());
    info.pSetLayouts = sets.data();
    info.pushConstantRangeCount = static_cast<std::uint32_t>(push.size());
    info.pPushConstantRanges = push.data();
    if (!check(api.vkCreatePipelineLayout(device, &info, nullptr, &out), "vkCreatePipelineLayout", error)) {
        return false;
    }
    const VkPipelineLayout layout = out;
    defer([this, layout] { api.vkDestroyPipelineLayout(device, layout, nullptr); });
    return true;
}

bool Kit::make_descriptor_set(VkDescriptorSet& out, VkDescriptorSetLayout layout, const std::vector<DescriptorWrite>& writes,
                              std::string& error) {
    std::vector<VkDescriptorPoolSize> sizes;
    for (const DescriptorWrite& w : writes) {
        auto it = std::find_if(sizes.begin(), sizes.end(), [&](const VkDescriptorPoolSize& s) { return s.type == w.type; });
        if (it == sizes.end()) {
            sizes.push_back({w.type, 1});
        } else {
            ++it->descriptorCount;
        }
    }
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    pool_info.pPoolSizes = sizes.data();
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    if (!check(api.vkCreateDescriptorPool(device, &pool_info, nullptr, &dpool), "vkCreateDescriptorPool", error)) {
        return false;
    }
    defer([this, dpool] { api.vkDestroyDescriptorPool(device, dpool, nullptr); });
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = dpool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;
    if (!check(api.vkAllocateDescriptorSets(device, &alloc, &out), "vkAllocateDescriptorSets", error)) {
        return false;
    }
    std::vector<VkWriteDescriptorSet> updates;
    updates.reserve(writes.size());
    for (const DescriptorWrite& w : writes) {
        VkWriteDescriptorSet update{};
        update.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        update.dstSet = out;
        update.dstBinding = w.binding;
        update.descriptorCount = 1;
        update.descriptorType = w.type;
        update.pBufferInfo = &w.buffer;
        update.pImageInfo = &w.image;
        updates.push_back(update);
    }
    api.vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(updates.size()), updates.data(), 0, nullptr);
    return true;
}

bool Kit::make_pipeline(VkPipeline& out, const PipelineDesc& desc, std::string& error) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = desc.vertex;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = desc.fragment;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertex{};
    vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex.vertexBindingDescriptionCount = static_cast<std::uint32_t>(desc.bindings.size());
    vertex.pVertexBindingDescriptions = desc.bindings.data();
    vertex.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(desc.attributes.size());
    vertex.pVertexAttributeDescriptions = desc.attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = desc.topology;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = desc.polygon;
    raster.cullMode = desc.cull;
    raster.frontFace = desc.front;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = desc.samples;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = desc.depth_op;
    depth.stencilTestEnable = desc.stencil_test ? VK_TRUE : VK_FALSE;
    depth.front = desc.stencil;
    depth.back = desc.stencil;
    depth.maxDepthBounds = 1.0f;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = desc.blend ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = desc.src_color;
    attachment.dstColorBlendFactor = desc.dst_color;
    attachment.colorBlendOp = desc.color_op;
    attachment.srcAlphaBlendFactor = desc.src_alpha;
    attachment.dstAlphaBlendFactor = desc.dst_alpha;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = desc.write_mask;
    std::vector<VkPipelineColorBlendAttachmentState> attachments(desc.color_attachments, attachment);
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    blend.pAttachments = attachments.data();
    const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_info{};
    dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_info.dynamicStateCount = 2;
    dynamic_info.pDynamicStates = dynamic;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic_info;
    info.layout = desc.layout;
    info.renderPass = desc.pass;
    info.subpass = desc.subpass;
    if (!check(api.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &out), "vkCreateGraphicsPipelines", error)) {
        return false;
    }
    const VkPipeline pipeline = out;
    defer([this, pipeline] { api.vkDestroyPipeline(device, pipeline, nullptr); });
    return true;
}

bool Kit::make_compute_pipeline(VkPipeline& out, VkShaderModule module, VkPipelineLayout layout, std::string& error) {
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    info.layout = layout;
    if (!check(api.vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &out), "vkCreateComputePipelines", error)) {
        return false;
    }
    const VkPipeline pipeline = out;
    defer([this, pipeline] { api.vkDestroyPipeline(device, pipeline, nullptr); });
    return true;
}

bool Kit::make_semaphore(VkSemaphore& out, std::string& error) {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (!check(api.vkCreateSemaphore(device, &info, nullptr, &out), "vkCreateSemaphore", error)) {
        return false;
    }
    const VkSemaphore semaphore = out;
    defer([this, semaphore] { api.vkDestroySemaphore(device, semaphore, nullptr); });
    return true;
}

bool Kit::submit(const std::function<void(VkCommandBuffer)>& record, std::string& error) {
    return submit_sync(record, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, error);
}

bool Kit::submit_sync(const std::function<void(VkCommandBuffer)>& record, VkSemaphore wait, VkPipelineStageFlags wait_stage,
                      VkSemaphore signal, std::string& error) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(api.vkResetCommandBuffer(command_, 0), "vkResetCommandBuffer", error) ||
        !check(api.vkBeginCommandBuffer(command_, &begin), "vkBeginCommandBuffer", error)) {
        return false;
    }
    record(command_);
    if (!check(api.vkEndCommandBuffer(command_), "vkEndCommandBuffer", error)) {
        return false;
    }
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = wait != VK_NULL_HANDLE ? 1 : 0;
    submit_info.pWaitSemaphores = &wait;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_;
    submit_info.signalSemaphoreCount = signal != VK_NULL_HANDLE ? 1 : 0;
    submit_info.pSignalSemaphores = &signal;
    if (!check(api.vkQueueSubmit(queue, 1, &submit_info, fence_), "vkQueueSubmit", error)) {
        return false;
    }
    const VkResult waited = api.vkWaitForFences(device, 1, &fence_, VK_TRUE, 30000000000ull);
    api.vkResetFences(device, 1, &fence_);
    return check(waited, "vkWaitForFences", error);
}

void Kit::barrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                  std::uint32_t mips, std::uint32_t layers) const {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, mips, 0, layers};
    api.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &b);
}

PixelKind Kit::pixel_kind(VkFormat format, VkImageAspectFlags aspect) {
    if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
        return PixelKind::R8Uint;
    }
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return PixelKind::Rgba8Unorm;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return PixelKind::Bgra8Unorm;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return PixelKind::Rgba16Float;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return PixelKind::Rgba32Float;
        case VK_FORMAT_R32_SFLOAT:
            return PixelKind::R32Float;
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return PixelKind::Depth32Float;
        case VK_FORMAT_R32G32B32A32_UINT:
            return PixelKind::Rgba32Uint;
        case VK_FORMAT_R8_UINT:
            return PixelKind::R8Uint;
        default:
            return PixelKind::Rgba8Unorm;
    }
}

std::uint32_t Kit::texel_bytes(VkFormat format, VkImageAspectFlags aspect) {
    return static_cast<std::uint32_t>(PixelInfo::bytes(pixel_kind(format, aspect)));
}

bool Kit::read_buffer(const Buffer& buffer, VkDeviceSize offset, VkDeviceSize size, std::vector<std::uint8_t>& out,
                      std::string& error) {
    Buffer staging;
    if (!make_buffer(staging, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, error)) {
        return false;
    }
    const bool ok = submit(
        [&](VkCommandBuffer cb) {
            const VkBufferCopy copy{offset, 0, size};
            api.vkCmdCopyBuffer(cb, buffer.buffer, staging.buffer, 1, &copy);
        },
        error);
    if (!ok) {
        return false;
    }
    out.assign(static_cast<std::uint8_t*>(staging.mapped), static_cast<std::uint8_t*>(staging.mapped) + size);
    return true;
}

bool Kit::read_texture(const Texture& texture, VkImageLayout layout, VkImageAspectFlags aspect, std::uint32_t layer, Image& out,
                       std::string& error) {
    const PixelKind kind = pixel_kind(texture.format, aspect);
    Image image(texture.width, texture.height, kind);
    Buffer staging;
    if (!make_buffer(staging, image.bytes().size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, error)) {
        return false;
    }
    const bool convert = layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && layout != VK_IMAGE_LAYOUT_GENERAL;
    const bool ok = submit(
        [&](VkCommandBuffer cb) {
            if (convert) {
                barrier(cb, texture.image, texture.aspect, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.mips, texture.layers);
            }
            VkBufferImageCopy region{};
            region.imageSubresource = {aspect, 0, layer, 1};
            region.imageExtent = {texture.width, texture.height, 1};
            api.vkCmdCopyImageToBuffer(cb, texture.image, convert ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : layout, staging.buffer, 1,
                                       &region);
            if (convert) {
                barrier(cb, texture.image, texture.aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, texture.mips, texture.layers);
            }
        },
        error);
    if (!ok) {
        return false;
    }
    std::memcpy(image.bytes().data(), staging.mapped, image.bytes().size());
    out = std::move(image);
    return true;
}

}  // namespace lsw::render::vk
