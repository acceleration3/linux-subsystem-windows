#include "support/vulkan_backend.hpp"

#include <algorithm>
#include <cstring>
#include <tuple>

namespace lsw::suites {

namespace {

class BufferVk : public GpuBuffer {
public:
    ~BufferVk() override {
        if (owner != nullptr) {
            owner->drain();
        }
        if (state != nullptr) {
            if (mapped != nullptr) {
                state->unmap_memory(state->device, memory);
            }
            state->destroy_buffer(state->device, buffer, nullptr);
            state->free_memory(state->device, memory, nullptr);
        }
    }

    VulkanBackend* owner = nullptr;
    VulkanDeviceState* state = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    bool coherent = true;
};

bool is_oom(VkResult result) {
    return result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY;
}

struct Raw {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

VkResult make_raw(VulkanDeviceState& state, std::uint64_t size, VkMemoryPropertyFlags required,
                  VkMemoryPropertyFlags preferred, bool map, Raw& out, bool* coherent = nullptr) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = state.create_buffer(state.device, &info, nullptr, &out.buffer);
    if (result != VK_SUCCESS) {
        out.buffer = VK_NULL_HANDLE;
        return result;
    }
    VkMemoryRequirements requirements{};
    state.buffer_requirements(state.device, out.buffer, &requirements);
    const int type = state.find_memory_type(requirements.memoryTypeBits, required, preferred);
    if (type < 0) {
        state.destroy_buffer(state.device, out.buffer, nullptr);
        out.buffer = VK_NULL_HANDLE;
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (coherent != nullptr) {
        *coherent = (state.memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
    result = state.allocate_memory(state.device, &allocate, nullptr, &out.memory);
    if (result == VK_SUCCESS) {
        result = state.bind_buffer(state.device, out.buffer, out.memory, 0);
    }
    if (result == VK_SUCCESS && map) {
        result = state.map_memory(state.device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped);
    }
    if (result != VK_SUCCESS) {
        state.destroy_buffer(state.device, out.buffer, nullptr);
        if (out.memory != VK_NULL_HANDLE) {
            state.free_memory(state.device, out.memory, nullptr);
        }
        out = {};
    }
    return result;
}

void copy_with_barrier(VulkanDeviceState& state, VkCommandBuffer commands, VkBuffer source, VkBuffer destination,
                       const VkBufferCopy& region) {
    state.cmd_copy(commands, source, destination, 1, &region);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    const auto cmd_barrier =
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(state.get_device_proc(state.device, "vkCmdPipelineBarrier"));
    cmd_barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0,
                nullptr);
}

constexpr VkMemoryPropertyFlags kHost = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
constexpr VkMemoryPropertyFlags kHostPreferred = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

}  // namespace

VulkanBackend::~VulkanBackend() {
    if (state_ == nullptr) {
        return;
    }
    std::string error;
    if (pool_ != VK_NULL_HANDLE) {
        static_cast<void>(submit(error));
    }
    for (auto [buffer, memory, mapped] : {std::tuple{ring_, ring_memory_, ring_map_}, std::tuple{readback_, readback_memory_, readback_map_}}) {
        if (buffer != VK_NULL_HANDLE) {
            state_->unmap_memory(state_->device, memory);
            state_->destroy_buffer(state_->device, buffer, nullptr);
            state_->free_memory(state_->device, memory, nullptr);
        }
        static_cast<void>(mapped);
    }
    if (pool_ != VK_NULL_HANDLE) {
        state_->destroy_pool(state_->device, pool_, nullptr);
    }
}

bool VulkanBackend::setup_commands(std::string& error) {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = state_->family;
    VkResult result = state_->create_pool(state_->device, &pool_info, nullptr, &pool_);
    if (result == VK_SUCCESS) {
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = pool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        result = state_->allocate_commands(state_->device, &allocate, &commands_);
    }
    if (result != VK_SUCCESS) {
        error = "creating the Vulkan command pool failed: " + std::to_string(result);
        return false;
    }
    return true;
}

bool VulkanBackend::init(const LUID& luid, std::string& error) {
    state_ = std::make_shared<VulkanDeviceState>();
    return state_->create(luid, error) && setup_commands(error);
}

std::unique_ptr<GpuBackend> VulkanBackend::new_context() {
    auto other = std::make_unique<VulkanBackend>();
    other->state_ = state_;
    std::string error;
    if (!other->setup_commands(error)) {
        return nullptr;
    }
    return other;
}

bool VulkanBackend::begin(std::string& error) {
    if (open_) {
        return true;
    }
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    state_->reset_commands(commands_, 0);
    const VkResult result = state_->begin_commands(commands_, &info);
    if (result != VK_SUCCESS) {
        error = "vkBeginCommandBuffer failed: " + std::to_string(result);
        return false;
    }
    open_ = true;
    return true;
}

bool VulkanBackend::submit(std::string& error) {
    if (!open_) {
        return true;
    }
    open_ = false;
    VkResult result = state_->end_commands(commands_);
    if (result != VK_SUCCESS) {
        error = "vkEndCommandBuffer failed: " + std::to_string(result);
        return false;
    }
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &commands_;
    {
        std::lock_guard lock(state_->mutex);
        result = state_->queue_submit(state_->queue, 1, &submit_info, VK_NULL_HANDLE);
        if (result == VK_SUCCESS) {
            result = state_->queue_wait(state_->queue);
        }
    }
    ring_head_ = 0;
    if (result != VK_SUCCESS) {
        error = "submitting Vulkan work failed: " + std::to_string(result);
        return false;
    }
    return true;
}

bool VulkanBackend::stage(const void* data, std::uint64_t size, std::uint64_t& offset, std::string& error) {
    const std::uint64_t needed = (size + 255) & ~255ull;
    if (ring_ == VK_NULL_HANDLE || needed > ring_size_ - ring_head_) {
        if (!submit(error)) {
            return false;
        }
        if (ring_ == VK_NULL_HANDLE || needed > ring_size_) {
            if (ring_ != VK_NULL_HANDLE) {
                state_->unmap_memory(state_->device, ring_memory_);
                state_->destroy_buffer(state_->device, ring_, nullptr);
                state_->free_memory(state_->device, ring_memory_, nullptr);
                ring_ = VK_NULL_HANDLE;
            }
            ring_size_ = std::max<std::uint64_t>(needed, 16ull << 20);
            Raw raw;
            const VkResult result = make_raw(*state_, ring_size_, kHost, kHostPreferred, true, raw);
            if (result != VK_SUCCESS) {
                error = "creating the Vulkan staging buffer failed: " + std::to_string(result);
                return false;
            }
            ring_ = raw.buffer;
            ring_memory_ = raw.memory;
            ring_map_ = static_cast<std::uint8_t*>(raw.mapped);
        }
        ring_head_ = 0;
    }
    std::memcpy(ring_map_ + ring_head_, data, static_cast<std::size_t>(size));
    offset = ring_head_;
    ring_head_ += needed;
    return true;
}

bool VulkanBackend::ensure_readback(std::uint64_t size, std::string& error) {
    if (readback_ != VK_NULL_HANDLE && readback_size_ >= size) {
        return true;
    }
    if (readback_ != VK_NULL_HANDLE) {
        state_->unmap_memory(state_->device, readback_memory_);
        state_->destroy_buffer(state_->device, readback_, nullptr);
        state_->free_memory(state_->device, readback_memory_, nullptr);
        readback_ = VK_NULL_HANDLE;
    }
    readback_size_ = std::max<std::uint64_t>(size, 4ull << 20);
    Raw raw;
    const VkResult result = make_raw(*state_, readback_size_, kHost, VK_MEMORY_PROPERTY_HOST_CACHED_BIT | kHostPreferred, true, raw);
    if (result != VK_SUCCESS) {
        error = "creating the Vulkan readback buffer failed: " + std::to_string(result);
        return false;
    }
    readback_ = raw.buffer;
    readback_memory_ = raw.memory;
    readback_map_ = static_cast<std::uint8_t*>(raw.mapped);
    return true;
}

std::unique_ptr<GpuBuffer> VulkanBackend::create(std::uint64_t size, BufferKind kind, AllocStatus& status,
                                                 std::string& detail) {
    if (size == 0) {
        status = AllocStatus::Failed;
        detail = "zero-sized buffers are invalid usage in Vulkan and are not issued";
        return nullptr;
    }
    Raw raw;
    bool coherent = true;
    const VkResult result =
        kind == BufferKind::Device
            ? make_raw(*state_, size, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, raw)
            : make_raw(*state_, size, kHost,
                       kind == BufferKind::Readback ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT | kHostPreferred : kHostPreferred,
                       false, raw, &coherent);
    if (result != VK_SUCCESS) {
        status = is_oom(result) ? AllocStatus::OutOfMemory : AllocStatus::Failed;
        detail = "Vulkan buffer creation failed: " + std::to_string(result);
        return nullptr;
    }
    auto buffer = std::make_unique<BufferVk>();
    buffer->owner = this;
    buffer->state = state_.get();
    buffer->buffer = raw.buffer;
    buffer->memory = raw.memory;
    buffer->size = size;
    buffer->kind = kind;
    buffer->coherent = coherent;
    status = AllocStatus::Ok;
    return buffer;
}

bool VulkanBackend::map(GpuBuffer& buffer, void** pointer, std::string& error) {
    auto& b = static_cast<BufferVk&>(buffer);
    if (b.kind == BufferKind::Device) {
        error = "device buffers cannot be mapped";
        return false;
    }
    if (b.mapped == nullptr) {
        const VkResult result = state_->map_memory(state_->device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
        if (result != VK_SUCCESS) {
            error = "vkMapMemory failed: " + std::to_string(result);
            return false;
        }
    }
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = b.memory;
    range.size = VK_WHOLE_SIZE;
    state_->invalidate_ranges(state_->device, 1, &range);
    *pointer = b.mapped;
    return true;
}

bool VulkanBackend::unmap(GpuBuffer& buffer, std::string&) {
    auto& b = static_cast<BufferVk&>(buffer);
    if (b.mapped != nullptr) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = b.memory;
        range.size = VK_WHOLE_SIZE;
        state_->flush_ranges(state_->device, 1, &range);
        state_->unmap_memory(state_->device, b.memory);
        b.mapped = nullptr;
    }
    return true;
}

bool VulkanBackend::upload(GpuBuffer& buffer, std::uint64_t offset, const void* data, std::uint64_t size,
                           std::string& error) {
    auto& b = static_cast<BufferVk&>(buffer);
    if (b.kind != BufferKind::Device) {
        void* pointer = nullptr;
        const bool was_mapped = b.mapped != nullptr;
        if (!map(buffer, &pointer, error)) {
            return false;
        }
        std::memcpy(static_cast<char*>(pointer) + offset, data, static_cast<std::size_t>(size));
        if (!was_mapped) {
            return unmap(buffer, error);
        }
        return true;
    }
    std::uint64_t staged = 0;
    if (!stage(data, size, staged, error) || !begin(error)) {
        return false;
    }
    copy_with_barrier(*state_, commands_, ring_, b.buffer, VkBufferCopy{staged, offset, size});
    return true;
}

bool VulkanBackend::download(GpuBuffer& buffer, std::uint64_t offset, void* out, std::uint64_t size,
                             std::string& error) {
    auto& b = static_cast<BufferVk&>(buffer);
    if (b.kind != BufferKind::Device) {
        if (!submit(error)) {
            return false;
        }
        void* pointer = nullptr;
        const bool was_mapped = b.mapped != nullptr;
        if (!map(buffer, &pointer, error)) {
            return false;
        }
        std::memcpy(out, static_cast<char*>(pointer) + offset, static_cast<std::size_t>(size));
        if (!was_mapped) {
            return unmap(buffer, error);
        }
        return true;
    }
    if (!ensure_readback(size, error) || !begin(error)) {
        return false;
    }
    copy_with_barrier(*state_, commands_, b.buffer, readback_, VkBufferCopy{offset, 0, size});
    if (!submit(error)) {
        return false;
    }
    std::memcpy(out, readback_map_, static_cast<std::size_t>(size));
    return true;
}

bool VulkanBackend::copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                         std::uint64_t destination_offset, std::uint64_t size, std::string& error) {
    if (!begin(error)) {
        return false;
    }
    copy_with_barrier(*state_, commands_, static_cast<BufferVk&>(source).buffer,
                      static_cast<BufferVk&>(destination).buffer, VkBufferCopy{source_offset, destination_offset, size});
    return true;
}

namespace {

struct ImportWin32 {
    VkStructureType sType;
    const void* pNext;
    VkExternalMemoryHandleTypeFlagBits handleType;
    HANDLE handle;
    LPCWSTR name;
};

constexpr VkStructureType kImportWin32Type = static_cast<VkStructureType>(1000073000);

}  // namespace

bool VulkanBackend::texture_import_supported(std::uint32_t handle_type, std::string& detail) {
    if (!state_->external_win32) {
        detail = "VK_KHR_external_memory_win32 is not exposed by the device";
        return false;
    }
    VkPhysicalDeviceExternalImageFormatInfo external{};
    external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(handle_type);
    VkPhysicalDeviceImageFormatInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.pNext = &external;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.type = VK_IMAGE_TYPE_2D;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkExternalImageFormatProperties external_properties{};
    external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    properties.pNext = &external_properties;
    const VkResult result = state_->image_format_properties2(state_->physical, &info, &properties);
    if (result != VK_SUCCESS) {
        detail = "vkGetPhysicalDeviceImageFormatProperties2 returned " + std::to_string(result);
        return false;
    }
    if ((external_properties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
        detail = "the handle type is not importable";
        return false;
    }
    return true;
}

bool VulkanBackend::read_imported_texture(HANDLE handle, std::uint32_t handle_type, std::uint32_t width, std::uint32_t height,
                                          std::vector<std::uint8_t>& tight, std::string& error) {
    VkExternalMemoryImageCreateInfo external{};
    external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external.handleTypes = handle_type;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = &external;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    VkResult result = state_->create_image(state_->device, &info, nullptr, &image);
    if (result != VK_SUCCESS) {
        error = "vkCreateImage failed: " + std::to_string(result);
        return false;
    }
    VkMemoryRequirements requirements{};
    state_->image_requirements(state_->device, image, &requirements);
    const int type = state_->find_memory_type(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (type >= 0) {
        VkMemoryDedicatedAllocateInfo dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.image = image;
        ImportWin32 import_info{kImportWin32Type, &dedicated, static_cast<VkExternalMemoryHandleTypeFlagBits>(handle_type), handle, nullptr};
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.pNext = &import_info;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = static_cast<std::uint32_t>(type);
        result = state_->allocate_memory(state_->device, &allocate, nullptr, &memory);
    } else {
        result = VK_ERROR_FEATURE_NOT_PRESENT;
    }
    bool ok = result == VK_SUCCESS && state_->bind_image(state_->device, image, memory, 0) == VK_SUCCESS;
    if (!ok) {
        error = "importing the texture memory failed: " + std::to_string(result);
    }
    if (ok && ensure_readback(static_cast<std::uint64_t>(width) * height * 4, error) && begin(error)) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        state_->cmd_barrier(commands_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        state_->cmd_copy_image(commands_, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_, 1, &region);
        if (submit(error)) {
            tight.assign(readback_map_, readback_map_ + static_cast<std::size_t>(width) * height * 4);
        } else {
            ok = false;
        }
    } else {
        ok = false;
    }
    if (memory != VK_NULL_HANDLE) {
        state_->free_memory(state_->device, memory, nullptr);
    }
    state_->destroy_image(state_->device, image, nullptr);
    return ok;
}

void VulkanBackend::drain() {
    std::string error;
    static_cast<void>(submit(error));
}

bool VulkanBackend::finish(std::string& error) {
    return submit(error);
}

bool VulkanBackend::release_staging(std::string& error) {
    if (!submit(error)) {
        return false;
    }
    if (ring_ != VK_NULL_HANDLE) {
        state_->unmap_memory(state_->device, ring_memory_);
        state_->destroy_buffer(state_->device, ring_, nullptr);
        state_->free_memory(state_->device, ring_memory_, nullptr);
        ring_ = VK_NULL_HANDLE;
        ring_size_ = 0;
    }
    if (readback_ != VK_NULL_HANDLE) {
        state_->unmap_memory(state_->device, readback_memory_);
        state_->destroy_buffer(state_->device, readback_, nullptr);
        state_->free_memory(state_->device, readback_memory_, nullptr);
        readback_ = VK_NULL_HANDLE;
        readback_size_ = 0;
    }
    return true;
}

std::uint64_t VulkanBackend::heap_usage(bool device_local) {
    std::vector<VkDeviceSize> usage;
    std::vector<VkDeviceSize> limit;
    if (!state_->budget(usage, limit)) {
        return 0;
    }
    std::uint64_t total = 0;
    for (std::uint32_t h = 0; h < state_->memory.memoryHeapCount; ++h) {
        const bool local = (state_->memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        if (local == device_local) {
            total += usage[h];
        }
    }
    return total;
}

std::uint64_t VulkanBackend::available_memory() {
    std::vector<VkDeviceSize> usage;
    std::vector<VkDeviceSize> limit;
    const bool have = state_->budget(usage, limit);
    std::uint64_t best = 0;
    for (std::uint32_t h = 0; h < state_->memory.memoryHeapCount; ++h) {
        if ((state_->memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
            continue;
        }
        const std::uint64_t free_bytes = have ? (limit[h] > usage[h] ? limit[h] - usage[h] : 0)
                                              : state_->memory.memoryHeaps[h].size;
        best = std::max(best, free_bytes);
    }
    return best;
}

std::uint64_t VulkanBackend::oversize_request() {
    std::uint64_t total = 0;
    for (std::uint32_t h = 0; h < state_->memory.memoryHeapCount; ++h) {
        total += state_->memory.memoryHeaps[h].size;
    }
    return total * 2 + (1ull << 30);
}

}  // namespace lsw::suites
