#include "lsw/render/handlers/import_handler.hpp"

#include "lsw/base/log.hpp"
#include "lsw/protocol/buffer_reader.hpp"
#include "lsw/protocol/protocol_constants.hpp"

#include "lsw/linkproto/region.hpp"

#include <cstring>
#include <vulkan/vulkan_core.h>

namespace lsw::render::handlers {

namespace {

template<typename Handle>
[[nodiscard]] Handle unpack(std::uint64_t raw) noexcept {
    Handle out{};
    static_assert(sizeof(out) == sizeof(raw));
    std::memcpy(&out, &raw, sizeof(out));
    return out;
}

[[nodiscard]] std::uint64_t pack(VkDeviceMemory h) noexcept {
    std::uint64_t out = 0;
    static_assert(sizeof(h) == sizeof(out));
    std::memcpy(&out, &h, sizeof(out));
    return out;
}

// What the driver will accept as an imported host pointer, asked once
// per physical device.
//
// Queried at RUNTIME rather than assumed. Values of 4 KiB and 64 KiB
// are both in the wild, and a build that baked in either would work on
// the machine it was built on and silently fall back (or worse) on the
// next one.
[[nodiscard]] VkDeviceSize imported_pointer_alignment(VkPhysicalDevice phys) {
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host_props{};
    host_props.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &host_props;
    ::vkGetPhysicalDeviceProperties2(phys, &props);
    LSW_INFO("import probe: apiVersion={}.{}.{} minImportedHostPointerAlignment={}",
             VK_API_VERSION_MAJOR(props.properties.apiVersion),
             VK_API_VERSION_MINOR(props.properties.apiVersion),
             VK_API_VERSION_PATCH(props.properties.apiVersion),
             host_props.minImportedHostPointerAlignment);
    return host_props.minImportedHostPointerAlignment;
}

}  // namespace

HandleResult ImportHandler::handle(Session& /*session*/, std::uint32_t cmd_id,
                                   std::span<const std::uint8_t> payload,
                                   ::lsw::protocol::Buffer& reply) {
    if (cmd_id != ::lsw::protocol::kCmdAllocateImported) {
        return HandleResult::Pass;
    }

    ::lsw::protocol::BufferReader r{payload};
    std::uint64_t device_raw = 0;
    std::uint64_t size = 0;
    std::uint32_t memory_type_index = 0;
    std::uint64_t arena_offset = 0;
    if (!r.read_pod(device_raw) || !r.read_pod(size) || !r.read_pod(memory_type_index)
        || !r.read_pod(arena_offset)) {
        return HandleResult::ProtocolError;
    }

    // "Cannot import" is a normal reply, so every early exit writes the
    // same shape and the client always has something to act on.
    const auto decline = [&](const char* why) {
        LSW_DEBUG("import declined ({}): size={} offset={}", why, size, arena_offset);
        reply.write_pod<std::int32_t>(VK_SUCCESS);
        reply.write_pod<std::uint64_t>(0);
        reply.write_pod<std::uint8_t>(0);
        return HandleResult::Handled;
    };

    if (region_ == nullptr || !region_->valid()) {
        return decline("no shared region with this peer");
    }

    // The offset is relative to the HOSTMEM CHANNEL, not to the region.
    //
    // Getting this wrong is silent: the import succeeds, the application
    // renders, the frame count is right and the exit code is zero — and
    // the cube comes out black, because the GPU is reading bytes from
    // 0x1101000 short of where the client wrote them. Locate the channel
    // by kind, the same way every other peer does.
    const auto channel = linkproto::find_channel(region_->base(), linkproto::kChannelHostMem);
    if (!channel.valid()) {
        return decline("region has no hostmem channel");
    }
    if (arena_offset > channel.size || size > channel.size - arena_offset) {
        // A peer describing memory outside its own arena is either
        // confused or hostile; either way we are not handing the driver
        // a pointer past the end of it.
        LSW_WARN("import refused: [{}, {}) lies outside a {}-byte arena", arena_offset,
                 arena_offset + size, channel.size);
        return decline("range outside the hostmem arena");
    }

    auto* host_ptr =
        static_cast<std::uint8_t*>(region_->base()) + channel.offset + arena_offset;
    const VkDevice device = unpack<VkDevice>(device_raw);
    const VkPhysicalDevice phys = dispatch_.physical_device_for(device);
    if (phys == VK_NULL_HANDLE) {
        return decline("no physical device recorded for this VkDevice");
    }

    const VkDeviceSize alignment = imported_pointer_alignment(phys);
    if (alignment == 0) {
        // The extension is absent: the property struct was never
        // filled, so the driver cannot import anything.
        return decline("driver has no VK_EXT_external_memory_host");
    }
    if ((reinterpret_cast<std::uintptr_t>(host_ptr) % alignment) != 0) {
        // Where the region landed in this process is mmap's business,
        // and no client-side alignment can guarantee it. Declining is
        // the correct outcome, not a bug to work around.
        return decline("host pointer does not meet the driver's alignment");
    }
    if ((size % alignment) != 0) {
        return decline("size is not a multiple of the driver's alignment");
    }

    // The spec's actual requirement, and the one this originally
    // skipped: an imported host pointer may only back a memory type
    // named in ``VkMemoryHostPointerPropertiesEXT::memoryTypeBits``.
    //
    // Skipping it did not fail loudly. The driver accepted an import
    // into a type that was not importable, and the NEXT allocation
    // failed with VK_ERROR_OUT_OF_DEVICE_MEMORY — a device-local image
    // allocation, nowhere near the import, which then bound null memory
    // and hung the application. Asking the driver which types it will
    // accept is both correct and what makes the answer this machine's
    // rather than a guess.
    auto get_props = reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(
        ::vkGetDeviceProcAddr(device, "vkGetMemoryHostPointerPropertiesEXT"));
    if (get_props == nullptr) {
        return decline("vkGetMemoryHostPointerPropertiesEXT unavailable");
    }
    VkMemoryHostPointerPropertiesEXT ptr_props{};
    ptr_props.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;
    if (get_props(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, host_ptr,
                  &ptr_props)
        != VK_SUCCESS) {
        return decline("driver would not describe this host pointer");
    }
    if (memory_type_index >= 32
        || (ptr_props.memoryTypeBits & (1U << memory_type_index)) == 0) {
        // Typically a device-local type: exactly the case the plan says
        // keeps the copy path, decided by the driver rather than by
        // inspecting property flags ourselves.
        return decline("memory type is not importable from host memory");
    }

    VkImportMemoryHostPointerInfoEXT import_info{};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    import_info.pHostPointer = host_ptr;

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.pNext = &import_info;
    alloc.allocationSize = size;
    alloc.memoryTypeIndex = memory_type_index;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkResult rc = ::vkAllocateMemory(device, &alloc, nullptr, &memory);
    if (rc != VK_SUCCESS) {
        // The driver may refuse for reasons we cannot enumerate — the
        // memory type may not be importable, for instance. Its answer is
        // final and the client copies instead.
        return decline("driver refused the import");
    }

    LSW_DEBUG("imported {} bytes at region offset {} as VkDeviceMemory", size, arena_offset);
    reply.write_pod<std::int32_t>(VK_SUCCESS);
    reply.write_pod<std::uint64_t>(pack(memory));
    reply.write_pod<std::uint8_t>(1);
    return HandleResult::Handled;
}

}  // namespace lsw::render::handlers
