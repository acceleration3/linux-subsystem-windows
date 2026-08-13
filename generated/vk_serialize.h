#pragma once


#include <cstddef>
#include <cstdint>
#include <cstring>

#include <vulkan/vulkan_core.h>

#include "lsw/protocol/arena.hpp"
#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/buffer_reader.hpp"

namespace lsw::vkproto {

// All Vulkan handles cross the wire as 64-bit values. See docs/protocol.md.
// LSW currently requires 64-bit hosts; revisit handle width if 32-bit support
// becomes a goal.
static_assert(sizeof(void*) == 8, "LSW assumes 64-bit pointers");

// Hook for translating dispatchable handles to their wire form.
// The default implementations (linked from vk_handle_default.cpp,
// weakly bound) are identity — correct for the server replay path
// and for loopback tests. The ICD links a strong override that
// unwraps its loader-magic-marked LswWrappedHandle to the server's
// real driver handle.
[[nodiscard]] std::uint64_t handle_to_wire(VkInstance v) noexcept;
[[nodiscard]] std::uint64_t handle_to_wire(VkPhysicalDevice v) noexcept;
[[nodiscard]] std::uint64_t handle_to_wire(VkDevice v) noexcept;
[[nodiscard]] std::uint64_t handle_to_wire(VkQueue v) noexcept;
[[nodiscard]] std::uint64_t handle_to_wire(VkCommandBuffer v) noexcept;

inline void serialize_VkBuffer(::lsw::protocol::Buffer& buf, VkBuffer v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkBuffer) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkBuffer(::lsw::protocol::BufferReader& r, VkBuffer& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkBufferView(::lsw::protocol::Buffer& buf, VkBufferView v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkBufferView) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkBufferView(::lsw::protocol::BufferReader& r, VkBufferView& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkCommandBuffer(::lsw::protocol::Buffer& buf, VkCommandBuffer v) noexcept {
    buf.write_pod(handle_to_wire(v));
}

[[nodiscard]] inline bool deserialize_VkCommandBuffer(::lsw::protocol::BufferReader& r, VkCommandBuffer& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkCommandPool(::lsw::protocol::Buffer& buf, VkCommandPool v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkCommandPool) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkCommandPool(::lsw::protocol::BufferReader& r, VkCommandPool& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkDescriptorPool(::lsw::protocol::Buffer& buf, VkDescriptorPool v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkDescriptorPool) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkDescriptorPool(::lsw::protocol::BufferReader& r, VkDescriptorPool& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkDescriptorSet(::lsw::protocol::Buffer& buf, VkDescriptorSet v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkDescriptorSet) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkDescriptorSet(::lsw::protocol::BufferReader& r, VkDescriptorSet& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkDescriptorSetLayout(::lsw::protocol::Buffer& buf, VkDescriptorSetLayout v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkDescriptorSetLayout) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkDescriptorSetLayout(::lsw::protocol::BufferReader& r, VkDescriptorSetLayout& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkDescriptorUpdateTemplate(::lsw::protocol::Buffer& buf, VkDescriptorUpdateTemplate v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkDescriptorUpdateTemplate) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkDescriptorUpdateTemplate(::lsw::protocol::BufferReader& r, VkDescriptorUpdateTemplate& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkDevice(::lsw::protocol::Buffer& buf, VkDevice v) noexcept {
    buf.write_pod(handle_to_wire(v));
}

[[nodiscard]] inline bool deserialize_VkDevice(::lsw::protocol::BufferReader& r, VkDevice& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkDeviceMemory(::lsw::protocol::Buffer& buf, VkDeviceMemory v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkDeviceMemory) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkDeviceMemory(::lsw::protocol::BufferReader& r, VkDeviceMemory& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkEvent(::lsw::protocol::Buffer& buf, VkEvent v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkEvent) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkEvent(::lsw::protocol::BufferReader& r, VkEvent& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkFence(::lsw::protocol::Buffer& buf, VkFence v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkFence) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkFence(::lsw::protocol::BufferReader& r, VkFence& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkFramebuffer(::lsw::protocol::Buffer& buf, VkFramebuffer v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkFramebuffer) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkFramebuffer(::lsw::protocol::BufferReader& r, VkFramebuffer& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkImage(::lsw::protocol::Buffer& buf, VkImage v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkImage) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkImage(::lsw::protocol::BufferReader& r, VkImage& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkImageView(::lsw::protocol::Buffer& buf, VkImageView v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkImageView) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkImageView(::lsw::protocol::BufferReader& r, VkImageView& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkInstance(::lsw::protocol::Buffer& buf, VkInstance v) noexcept {
    buf.write_pod(handle_to_wire(v));
}

[[nodiscard]] inline bool deserialize_VkInstance(::lsw::protocol::BufferReader& r, VkInstance& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkPhysicalDevice(::lsw::protocol::Buffer& buf, VkPhysicalDevice v) noexcept {
    buf.write_pod(handle_to_wire(v));
}

[[nodiscard]] inline bool deserialize_VkPhysicalDevice(::lsw::protocol::BufferReader& r, VkPhysicalDevice& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkPipeline(::lsw::protocol::Buffer& buf, VkPipeline v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkPipeline) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkPipeline(::lsw::protocol::BufferReader& r, VkPipeline& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkPipelineCache(::lsw::protocol::Buffer& buf, VkPipelineCache v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkPipelineCache) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkPipelineCache(::lsw::protocol::BufferReader& r, VkPipelineCache& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkPipelineLayout(::lsw::protocol::Buffer& buf, VkPipelineLayout v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkPipelineLayout) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkPipelineLayout(::lsw::protocol::BufferReader& r, VkPipelineLayout& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkPrivateDataSlot(::lsw::protocol::Buffer& buf, VkPrivateDataSlot v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkPrivateDataSlot) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkPrivateDataSlot(::lsw::protocol::BufferReader& r, VkPrivateDataSlot& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkQueryPool(::lsw::protocol::Buffer& buf, VkQueryPool v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkQueryPool) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkQueryPool(::lsw::protocol::BufferReader& r, VkQueryPool& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkQueue(::lsw::protocol::Buffer& buf, VkQueue v) noexcept {
    buf.write_pod(handle_to_wire(v));
}

[[nodiscard]] inline bool deserialize_VkQueue(::lsw::protocol::BufferReader& r, VkQueue& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkRenderPass(::lsw::protocol::Buffer& buf, VkRenderPass v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkRenderPass) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkRenderPass(::lsw::protocol::BufferReader& r, VkRenderPass& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkSampler(::lsw::protocol::Buffer& buf, VkSampler v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkSampler) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkSampler(::lsw::protocol::BufferReader& r, VkSampler& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkSamplerYcbcrConversion(::lsw::protocol::Buffer& buf, VkSamplerYcbcrConversion v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkSamplerYcbcrConversion) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkSamplerYcbcrConversion(::lsw::protocol::BufferReader& r, VkSamplerYcbcrConversion& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkSemaphore(::lsw::protocol::Buffer& buf, VkSemaphore v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkSemaphore) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkSemaphore(::lsw::protocol::BufferReader& r, VkSemaphore& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkShaderModule(::lsw::protocol::Buffer& buf, VkShaderModule v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkShaderModule) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkShaderModule(::lsw::protocol::BufferReader& r, VkShaderModule& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkSurfaceKHR(::lsw::protocol::Buffer& buf, VkSurfaceKHR v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkSurfaceKHR) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkSurfaceKHR(::lsw::protocol::BufferReader& r, VkSurfaceKHR& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

inline void serialize_VkSwapchainKHR(::lsw::protocol::Buffer& buf, VkSwapchainKHR v) noexcept {
    std::uint64_t raw = 0;
    static_assert(sizeof(VkSwapchainKHR) == sizeof(raw));
    std::memcpy(&raw, &v, sizeof(raw));
    buf.write_pod(raw);
}

[[nodiscard]] inline bool deserialize_VkSwapchainKHR(::lsw::protocol::BufferReader& r, VkSwapchainKHR& out) noexcept {
    std::uint64_t raw = 0;
    if (!r.read_pod(raw)) {
        return false;
    }
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

// Forward declarations.
inline void serialize_VkAcquireNextImageInfoKHR(::lsw::protocol::Buffer&, const VkAcquireNextImageInfoKHR&);
[[nodiscard]] inline bool deserialize_VkAcquireNextImageInfoKHR(::lsw::protocol::BufferReader&, VkAcquireNextImageInfoKHR*, ::lsw::protocol::Arena&);
inline void serialize_VkApplicationInfo(::lsw::protocol::Buffer&, const VkApplicationInfo&);
[[nodiscard]] inline bool deserialize_VkApplicationInfo(::lsw::protocol::BufferReader&, VkApplicationInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkAttachmentDescription(::lsw::protocol::Buffer&, const VkAttachmentDescription&);
[[nodiscard]] inline bool deserialize_VkAttachmentDescription(::lsw::protocol::BufferReader&, VkAttachmentDescription*, ::lsw::protocol::Arena&);
inline void serialize_VkAttachmentDescription2(::lsw::protocol::Buffer&, const VkAttachmentDescription2&);
[[nodiscard]] inline bool deserialize_VkAttachmentDescription2(::lsw::protocol::BufferReader&, VkAttachmentDescription2*, ::lsw::protocol::Arena&);
inline void serialize_VkAttachmentReference(::lsw::protocol::Buffer&, const VkAttachmentReference&);
[[nodiscard]] inline bool deserialize_VkAttachmentReference(::lsw::protocol::BufferReader&, VkAttachmentReference*, ::lsw::protocol::Arena&);
inline void serialize_VkAttachmentReference2(::lsw::protocol::Buffer&, const VkAttachmentReference2&);
[[nodiscard]] inline bool deserialize_VkAttachmentReference2(::lsw::protocol::BufferReader&, VkAttachmentReference2*, ::lsw::protocol::Arena&);
inline void serialize_VkBindBufferMemoryInfo(::lsw::protocol::Buffer&, const VkBindBufferMemoryInfo&);
[[nodiscard]] inline bool deserialize_VkBindBufferMemoryInfo(::lsw::protocol::BufferReader&, VkBindBufferMemoryInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkBindImageMemoryInfo(::lsw::protocol::Buffer&, const VkBindImageMemoryInfo&);
[[nodiscard]] inline bool deserialize_VkBindImageMemoryInfo(::lsw::protocol::BufferReader&, VkBindImageMemoryInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkBindSparseInfo(::lsw::protocol::Buffer&, const VkBindSparseInfo&);
[[nodiscard]] inline bool deserialize_VkBindSparseInfo(::lsw::protocol::BufferReader&, VkBindSparseInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkBlitImageInfo2(::lsw::protocol::Buffer&, const VkBlitImageInfo2&);
[[nodiscard]] inline bool deserialize_VkBlitImageInfo2(::lsw::protocol::BufferReader&, VkBlitImageInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferCopy(::lsw::protocol::Buffer&, const VkBufferCopy&);
[[nodiscard]] inline bool deserialize_VkBufferCopy(::lsw::protocol::BufferReader&, VkBufferCopy*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferCopy2(::lsw::protocol::Buffer&, const VkBufferCopy2&);
[[nodiscard]] inline bool deserialize_VkBufferCopy2(::lsw::protocol::BufferReader&, VkBufferCopy2*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferCreateInfo(::lsw::protocol::Buffer&, const VkBufferCreateInfo&);
[[nodiscard]] inline bool deserialize_VkBufferCreateInfo(::lsw::protocol::BufferReader&, VkBufferCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferDeviceAddressInfo(::lsw::protocol::Buffer&, const VkBufferDeviceAddressInfo&);
[[nodiscard]] inline bool deserialize_VkBufferDeviceAddressInfo(::lsw::protocol::BufferReader&, VkBufferDeviceAddressInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkImageSubresourceLayers(::lsw::protocol::Buffer&, const VkImageSubresourceLayers&);
[[nodiscard]] inline bool deserialize_VkImageSubresourceLayers(::lsw::protocol::BufferReader&, VkImageSubresourceLayers*, ::lsw::protocol::Arena&);
inline void serialize_VkOffset3D(::lsw::protocol::Buffer&, const VkOffset3D&);
[[nodiscard]] inline bool deserialize_VkOffset3D(::lsw::protocol::BufferReader&, VkOffset3D*, ::lsw::protocol::Arena&);
inline void serialize_VkExtent3D(::lsw::protocol::Buffer&, const VkExtent3D&);
[[nodiscard]] inline bool deserialize_VkExtent3D(::lsw::protocol::BufferReader&, VkExtent3D*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferImageCopy(::lsw::protocol::Buffer&, const VkBufferImageCopy&);
[[nodiscard]] inline bool deserialize_VkBufferImageCopy(::lsw::protocol::BufferReader&, VkBufferImageCopy*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferImageCopy2(::lsw::protocol::Buffer&, const VkBufferImageCopy2&);
[[nodiscard]] inline bool deserialize_VkBufferImageCopy2(::lsw::protocol::BufferReader&, VkBufferImageCopy2*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferMemoryBarrier(::lsw::protocol::Buffer&, const VkBufferMemoryBarrier&);
[[nodiscard]] inline bool deserialize_VkBufferMemoryBarrier(::lsw::protocol::BufferReader&, VkBufferMemoryBarrier*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferMemoryBarrier2(::lsw::protocol::Buffer&, const VkBufferMemoryBarrier2&);
[[nodiscard]] inline bool deserialize_VkBufferMemoryBarrier2(::lsw::protocol::BufferReader&, VkBufferMemoryBarrier2*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferMemoryRequirementsInfo2(::lsw::protocol::Buffer&, const VkBufferMemoryRequirementsInfo2&);
[[nodiscard]] inline bool deserialize_VkBufferMemoryRequirementsInfo2(::lsw::protocol::BufferReader&, VkBufferMemoryRequirementsInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkBufferViewCreateInfo(::lsw::protocol::Buffer&, const VkBufferViewCreateInfo&);
[[nodiscard]] inline bool deserialize_VkBufferViewCreateInfo(::lsw::protocol::BufferReader&, VkBufferViewCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkClearColorValue(::lsw::protocol::Buffer&, const VkClearColorValue&);
[[nodiscard]] inline bool deserialize_VkClearColorValue(::lsw::protocol::BufferReader&, VkClearColorValue*, ::lsw::protocol::Arena&);
inline void serialize_VkClearDepthStencilValue(::lsw::protocol::Buffer&, const VkClearDepthStencilValue&);
[[nodiscard]] inline bool deserialize_VkClearDepthStencilValue(::lsw::protocol::BufferReader&, VkClearDepthStencilValue*, ::lsw::protocol::Arena&);
inline void serialize_VkClearValue(::lsw::protocol::Buffer&, const VkClearValue&);
[[nodiscard]] inline bool deserialize_VkClearValue(::lsw::protocol::BufferReader&, VkClearValue*, ::lsw::protocol::Arena&);
inline void serialize_VkClearAttachment(::lsw::protocol::Buffer&, const VkClearAttachment&);
[[nodiscard]] inline bool deserialize_VkClearAttachment(::lsw::protocol::BufferReader&, VkClearAttachment*, ::lsw::protocol::Arena&);
inline void serialize_VkOffset2D(::lsw::protocol::Buffer&, const VkOffset2D&);
[[nodiscard]] inline bool deserialize_VkOffset2D(::lsw::protocol::BufferReader&, VkOffset2D*, ::lsw::protocol::Arena&);
inline void serialize_VkExtent2D(::lsw::protocol::Buffer&, const VkExtent2D&);
[[nodiscard]] inline bool deserialize_VkExtent2D(::lsw::protocol::BufferReader&, VkExtent2D*, ::lsw::protocol::Arena&);
inline void serialize_VkRect2D(::lsw::protocol::Buffer&, const VkRect2D&);
[[nodiscard]] inline bool deserialize_VkRect2D(::lsw::protocol::BufferReader&, VkRect2D*, ::lsw::protocol::Arena&);
inline void serialize_VkClearRect(::lsw::protocol::Buffer&, const VkClearRect&);
[[nodiscard]] inline bool deserialize_VkClearRect(::lsw::protocol::BufferReader&, VkClearRect*, ::lsw::protocol::Arena&);
inline void serialize_VkCommandBufferAllocateInfo(::lsw::protocol::Buffer&, const VkCommandBufferAllocateInfo&);
[[nodiscard]] inline bool deserialize_VkCommandBufferAllocateInfo(::lsw::protocol::BufferReader&, VkCommandBufferAllocateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkCommandBufferBeginInfo(::lsw::protocol::Buffer&, const VkCommandBufferBeginInfo&);
[[nodiscard]] inline bool deserialize_VkCommandBufferBeginInfo(::lsw::protocol::BufferReader&, VkCommandBufferBeginInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkCommandBufferInheritanceInfo(::lsw::protocol::Buffer&, const VkCommandBufferInheritanceInfo&);
[[nodiscard]] inline bool deserialize_VkCommandBufferInheritanceInfo(::lsw::protocol::BufferReader&, VkCommandBufferInheritanceInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkCommandBufferSubmitInfo(::lsw::protocol::Buffer&, const VkCommandBufferSubmitInfo&);
[[nodiscard]] inline bool deserialize_VkCommandBufferSubmitInfo(::lsw::protocol::BufferReader&, VkCommandBufferSubmitInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkCommandPoolCreateInfo(::lsw::protocol::Buffer&, const VkCommandPoolCreateInfo&);
[[nodiscard]] inline bool deserialize_VkCommandPoolCreateInfo(::lsw::protocol::BufferReader&, VkCommandPoolCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkComponentMapping(::lsw::protocol::Buffer&, const VkComponentMapping&);
[[nodiscard]] inline bool deserialize_VkComponentMapping(::lsw::protocol::BufferReader&, VkComponentMapping*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineShaderStageCreateInfo(::lsw::protocol::Buffer&, const VkPipelineShaderStageCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineShaderStageCreateInfo(::lsw::protocol::BufferReader&, VkPipelineShaderStageCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkComputePipelineCreateInfo(::lsw::protocol::Buffer&, const VkComputePipelineCreateInfo&);
[[nodiscard]] inline bool deserialize_VkComputePipelineCreateInfo(::lsw::protocol::BufferReader&, VkComputePipelineCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkCopyBufferInfo2(::lsw::protocol::Buffer&, const VkCopyBufferInfo2&);
[[nodiscard]] inline bool deserialize_VkCopyBufferInfo2(::lsw::protocol::BufferReader&, VkCopyBufferInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkCopyBufferToImageInfo2(::lsw::protocol::Buffer&, const VkCopyBufferToImageInfo2&);
[[nodiscard]] inline bool deserialize_VkCopyBufferToImageInfo2(::lsw::protocol::BufferReader&, VkCopyBufferToImageInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkCopyDescriptorSet(::lsw::protocol::Buffer&, const VkCopyDescriptorSet&);
[[nodiscard]] inline bool deserialize_VkCopyDescriptorSet(::lsw::protocol::BufferReader&, VkCopyDescriptorSet*, ::lsw::protocol::Arena&);
inline void serialize_VkCopyImageInfo2(::lsw::protocol::Buffer&, const VkCopyImageInfo2&);
[[nodiscard]] inline bool deserialize_VkCopyImageInfo2(::lsw::protocol::BufferReader&, VkCopyImageInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkCopyImageToBufferInfo2(::lsw::protocol::Buffer&, const VkCopyImageToBufferInfo2&);
[[nodiscard]] inline bool deserialize_VkCopyImageToBufferInfo2(::lsw::protocol::BufferReader&, VkCopyImageToBufferInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkDependencyInfo(::lsw::protocol::Buffer&, const VkDependencyInfo&);
[[nodiscard]] inline bool deserialize_VkDependencyInfo(::lsw::protocol::BufferReader&, VkDependencyInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorBufferInfo(::lsw::protocol::Buffer&, const VkDescriptorBufferInfo&);
[[nodiscard]] inline bool deserialize_VkDescriptorBufferInfo(::lsw::protocol::BufferReader&, VkDescriptorBufferInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorImageInfo(::lsw::protocol::Buffer&, const VkDescriptorImageInfo&);
[[nodiscard]] inline bool deserialize_VkDescriptorImageInfo(::lsw::protocol::BufferReader&, VkDescriptorImageInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorPoolCreateInfo(::lsw::protocol::Buffer&, const VkDescriptorPoolCreateInfo&);
[[nodiscard]] inline bool deserialize_VkDescriptorPoolCreateInfo(::lsw::protocol::BufferReader&, VkDescriptorPoolCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorPoolSize(::lsw::protocol::Buffer&, const VkDescriptorPoolSize&);
[[nodiscard]] inline bool deserialize_VkDescriptorPoolSize(::lsw::protocol::BufferReader&, VkDescriptorPoolSize*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorSetAllocateInfo(::lsw::protocol::Buffer&, const VkDescriptorSetAllocateInfo&);
[[nodiscard]] inline bool deserialize_VkDescriptorSetAllocateInfo(::lsw::protocol::BufferReader&, VkDescriptorSetAllocateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorSetLayoutBinding(::lsw::protocol::Buffer&, const VkDescriptorSetLayoutBinding&);
[[nodiscard]] inline bool deserialize_VkDescriptorSetLayoutBinding(::lsw::protocol::BufferReader&, VkDescriptorSetLayoutBinding*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorSetLayoutCreateInfo(::lsw::protocol::Buffer&, const VkDescriptorSetLayoutCreateInfo&);
[[nodiscard]] inline bool deserialize_VkDescriptorSetLayoutCreateInfo(::lsw::protocol::BufferReader&, VkDescriptorSetLayoutCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorSetLayoutSupport(::lsw::protocol::Buffer&, const VkDescriptorSetLayoutSupport&);
[[nodiscard]] inline bool deserialize_VkDescriptorSetLayoutSupport(::lsw::protocol::BufferReader&, VkDescriptorSetLayoutSupport*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorUpdateTemplateCreateInfo(::lsw::protocol::Buffer&, const VkDescriptorUpdateTemplateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkDescriptorUpdateTemplateCreateInfo(::lsw::protocol::BufferReader&, VkDescriptorUpdateTemplateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDescriptorUpdateTemplateEntry(::lsw::protocol::Buffer&, const VkDescriptorUpdateTemplateEntry&);
[[nodiscard]] inline bool deserialize_VkDescriptorUpdateTemplateEntry(::lsw::protocol::BufferReader&, VkDescriptorUpdateTemplateEntry*, ::lsw::protocol::Arena&);
inline void serialize_VkDeviceBufferMemoryRequirements(::lsw::protocol::Buffer&, const VkDeviceBufferMemoryRequirements&);
[[nodiscard]] inline bool deserialize_VkDeviceBufferMemoryRequirements(::lsw::protocol::BufferReader&, VkDeviceBufferMemoryRequirements*, ::lsw::protocol::Arena&);
inline void serialize_VkDeviceCreateInfo(::lsw::protocol::Buffer&, const VkDeviceCreateInfo&);
[[nodiscard]] inline bool deserialize_VkDeviceCreateInfo(::lsw::protocol::BufferReader&, VkDeviceCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDeviceGroupPresentCapabilitiesKHR(::lsw::protocol::Buffer&, const VkDeviceGroupPresentCapabilitiesKHR&);
[[nodiscard]] inline bool deserialize_VkDeviceGroupPresentCapabilitiesKHR(::lsw::protocol::BufferReader&, VkDeviceGroupPresentCapabilitiesKHR*, ::lsw::protocol::Arena&);
inline void serialize_VkDeviceImageMemoryRequirements(::lsw::protocol::Buffer&, const VkDeviceImageMemoryRequirements&);
[[nodiscard]] inline bool deserialize_VkDeviceImageMemoryRequirements(::lsw::protocol::BufferReader&, VkDeviceImageMemoryRequirements*, ::lsw::protocol::Arena&);
inline void serialize_VkDeviceMemoryOpaqueCaptureAddressInfo(::lsw::protocol::Buffer&, const VkDeviceMemoryOpaqueCaptureAddressInfo&);
[[nodiscard]] inline bool deserialize_VkDeviceMemoryOpaqueCaptureAddressInfo(::lsw::protocol::BufferReader&, VkDeviceMemoryOpaqueCaptureAddressInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDeviceQueueCreateInfo(::lsw::protocol::Buffer&, const VkDeviceQueueCreateInfo&);
[[nodiscard]] inline bool deserialize_VkDeviceQueueCreateInfo(::lsw::protocol::BufferReader&, VkDeviceQueueCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkDeviceQueueInfo2(::lsw::protocol::Buffer&, const VkDeviceQueueInfo2&);
[[nodiscard]] inline bool deserialize_VkDeviceQueueInfo2(::lsw::protocol::BufferReader&, VkDeviceQueueInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkEventCreateInfo(::lsw::protocol::Buffer&, const VkEventCreateInfo&);
[[nodiscard]] inline bool deserialize_VkEventCreateInfo(::lsw::protocol::BufferReader&, VkEventCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkExtensionProperties(::lsw::protocol::Buffer&, const VkExtensionProperties&);
[[nodiscard]] inline bool deserialize_VkExtensionProperties(::lsw::protocol::BufferReader&, VkExtensionProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkExternalMemoryProperties(::lsw::protocol::Buffer&, const VkExternalMemoryProperties&);
[[nodiscard]] inline bool deserialize_VkExternalMemoryProperties(::lsw::protocol::BufferReader&, VkExternalMemoryProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkExternalBufferProperties(::lsw::protocol::Buffer&, const VkExternalBufferProperties&);
[[nodiscard]] inline bool deserialize_VkExternalBufferProperties(::lsw::protocol::BufferReader&, VkExternalBufferProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkExternalFenceProperties(::lsw::protocol::Buffer&, const VkExternalFenceProperties&);
[[nodiscard]] inline bool deserialize_VkExternalFenceProperties(::lsw::protocol::BufferReader&, VkExternalFenceProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkExternalSemaphoreProperties(::lsw::protocol::Buffer&, const VkExternalSemaphoreProperties&);
[[nodiscard]] inline bool deserialize_VkExternalSemaphoreProperties(::lsw::protocol::BufferReader&, VkExternalSemaphoreProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkFenceCreateInfo(::lsw::protocol::Buffer&, const VkFenceCreateInfo&);
[[nodiscard]] inline bool deserialize_VkFenceCreateInfo(::lsw::protocol::BufferReader&, VkFenceCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkFormatProperties(::lsw::protocol::Buffer&, const VkFormatProperties&);
[[nodiscard]] inline bool deserialize_VkFormatProperties(::lsw::protocol::BufferReader&, VkFormatProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkFormatProperties2(::lsw::protocol::Buffer&, const VkFormatProperties2&);
[[nodiscard]] inline bool deserialize_VkFormatProperties2(::lsw::protocol::BufferReader&, VkFormatProperties2*, ::lsw::protocol::Arena&);
inline void serialize_VkFramebufferCreateInfo(::lsw::protocol::Buffer&, const VkFramebufferCreateInfo&);
[[nodiscard]] inline bool deserialize_VkFramebufferCreateInfo(::lsw::protocol::BufferReader&, VkFramebufferCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkGraphicsPipelineCreateInfo(::lsw::protocol::Buffer&, const VkGraphicsPipelineCreateInfo&);
[[nodiscard]] inline bool deserialize_VkGraphicsPipelineCreateInfo(::lsw::protocol::BufferReader&, VkGraphicsPipelineCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkImageBlit(::lsw::protocol::Buffer&, const VkImageBlit&);
[[nodiscard]] inline bool deserialize_VkImageBlit(::lsw::protocol::BufferReader&, VkImageBlit*, ::lsw::protocol::Arena&);
inline void serialize_VkImageBlit2(::lsw::protocol::Buffer&, const VkImageBlit2&);
[[nodiscard]] inline bool deserialize_VkImageBlit2(::lsw::protocol::BufferReader&, VkImageBlit2*, ::lsw::protocol::Arena&);
inline void serialize_VkImageCopy(::lsw::protocol::Buffer&, const VkImageCopy&);
[[nodiscard]] inline bool deserialize_VkImageCopy(::lsw::protocol::BufferReader&, VkImageCopy*, ::lsw::protocol::Arena&);
inline void serialize_VkImageCopy2(::lsw::protocol::Buffer&, const VkImageCopy2&);
[[nodiscard]] inline bool deserialize_VkImageCopy2(::lsw::protocol::BufferReader&, VkImageCopy2*, ::lsw::protocol::Arena&);
inline void serialize_VkImageCreateInfo(::lsw::protocol::Buffer&, const VkImageCreateInfo&);
[[nodiscard]] inline bool deserialize_VkImageCreateInfo(::lsw::protocol::BufferReader&, VkImageCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkImageFormatProperties(::lsw::protocol::Buffer&, const VkImageFormatProperties&);
[[nodiscard]] inline bool deserialize_VkImageFormatProperties(::lsw::protocol::BufferReader&, VkImageFormatProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkImageFormatProperties2(::lsw::protocol::Buffer&, const VkImageFormatProperties2&);
[[nodiscard]] inline bool deserialize_VkImageFormatProperties2(::lsw::protocol::BufferReader&, VkImageFormatProperties2*, ::lsw::protocol::Arena&);
inline void serialize_VkImageSubresourceRange(::lsw::protocol::Buffer&, const VkImageSubresourceRange&);
[[nodiscard]] inline bool deserialize_VkImageSubresourceRange(::lsw::protocol::BufferReader&, VkImageSubresourceRange*, ::lsw::protocol::Arena&);
inline void serialize_VkImageMemoryBarrier(::lsw::protocol::Buffer&, const VkImageMemoryBarrier&);
[[nodiscard]] inline bool deserialize_VkImageMemoryBarrier(::lsw::protocol::BufferReader&, VkImageMemoryBarrier*, ::lsw::protocol::Arena&);
inline void serialize_VkImageMemoryBarrier2(::lsw::protocol::Buffer&, const VkImageMemoryBarrier2&);
[[nodiscard]] inline bool deserialize_VkImageMemoryBarrier2(::lsw::protocol::BufferReader&, VkImageMemoryBarrier2*, ::lsw::protocol::Arena&);
inline void serialize_VkImageMemoryRequirementsInfo2(::lsw::protocol::Buffer&, const VkImageMemoryRequirementsInfo2&);
[[nodiscard]] inline bool deserialize_VkImageMemoryRequirementsInfo2(::lsw::protocol::BufferReader&, VkImageMemoryRequirementsInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkImageResolve(::lsw::protocol::Buffer&, const VkImageResolve&);
[[nodiscard]] inline bool deserialize_VkImageResolve(::lsw::protocol::BufferReader&, VkImageResolve*, ::lsw::protocol::Arena&);
inline void serialize_VkImageResolve2(::lsw::protocol::Buffer&, const VkImageResolve2&);
[[nodiscard]] inline bool deserialize_VkImageResolve2(::lsw::protocol::BufferReader&, VkImageResolve2*, ::lsw::protocol::Arena&);
inline void serialize_VkImageSparseMemoryRequirementsInfo2(::lsw::protocol::Buffer&, const VkImageSparseMemoryRequirementsInfo2&);
[[nodiscard]] inline bool deserialize_VkImageSparseMemoryRequirementsInfo2(::lsw::protocol::BufferReader&, VkImageSparseMemoryRequirementsInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkImageSubresource(::lsw::protocol::Buffer&, const VkImageSubresource&);
[[nodiscard]] inline bool deserialize_VkImageSubresource(::lsw::protocol::BufferReader&, VkImageSubresource*, ::lsw::protocol::Arena&);
inline void serialize_VkImageViewCreateInfo(::lsw::protocol::Buffer&, const VkImageViewCreateInfo&);
[[nodiscard]] inline bool deserialize_VkImageViewCreateInfo(::lsw::protocol::BufferReader&, VkImageViewCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkInstanceCreateInfo(::lsw::protocol::Buffer&, const VkInstanceCreateInfo&);
[[nodiscard]] inline bool deserialize_VkInstanceCreateInfo(::lsw::protocol::BufferReader&, VkInstanceCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkLayerProperties(::lsw::protocol::Buffer&, const VkLayerProperties&);
[[nodiscard]] inline bool deserialize_VkLayerProperties(::lsw::protocol::BufferReader&, VkLayerProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkMappedMemoryRange(::lsw::protocol::Buffer&, const VkMappedMemoryRange&);
[[nodiscard]] inline bool deserialize_VkMappedMemoryRange(::lsw::protocol::BufferReader&, VkMappedMemoryRange*, ::lsw::protocol::Arena&);
inline void serialize_VkMemoryAllocateInfo(::lsw::protocol::Buffer&, const VkMemoryAllocateInfo&);
[[nodiscard]] inline bool deserialize_VkMemoryAllocateInfo(::lsw::protocol::BufferReader&, VkMemoryAllocateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkMemoryBarrier(::lsw::protocol::Buffer&, const VkMemoryBarrier&);
[[nodiscard]] inline bool deserialize_VkMemoryBarrier(::lsw::protocol::BufferReader&, VkMemoryBarrier*, ::lsw::protocol::Arena&);
inline void serialize_VkMemoryBarrier2(::lsw::protocol::Buffer&, const VkMemoryBarrier2&);
[[nodiscard]] inline bool deserialize_VkMemoryBarrier2(::lsw::protocol::BufferReader&, VkMemoryBarrier2*, ::lsw::protocol::Arena&);
inline void serialize_VkMemoryHeap(::lsw::protocol::Buffer&, const VkMemoryHeap&);
[[nodiscard]] inline bool deserialize_VkMemoryHeap(::lsw::protocol::BufferReader&, VkMemoryHeap*, ::lsw::protocol::Arena&);
inline void serialize_VkMemoryRequirements(::lsw::protocol::Buffer&, const VkMemoryRequirements&);
[[nodiscard]] inline bool deserialize_VkMemoryRequirements(::lsw::protocol::BufferReader&, VkMemoryRequirements*, ::lsw::protocol::Arena&);
inline void serialize_VkMemoryRequirements2(::lsw::protocol::Buffer&, const VkMemoryRequirements2&);
[[nodiscard]] inline bool deserialize_VkMemoryRequirements2(::lsw::protocol::BufferReader&, VkMemoryRequirements2*, ::lsw::protocol::Arena&);
inline void serialize_VkMemoryType(::lsw::protocol::Buffer&, const VkMemoryType&);
[[nodiscard]] inline bool deserialize_VkMemoryType(::lsw::protocol::BufferReader&, VkMemoryType*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceExternalBufferInfo(::lsw::protocol::Buffer&, const VkPhysicalDeviceExternalBufferInfo&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceExternalBufferInfo(::lsw::protocol::BufferReader&, VkPhysicalDeviceExternalBufferInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceExternalFenceInfo(::lsw::protocol::Buffer&, const VkPhysicalDeviceExternalFenceInfo&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceExternalFenceInfo(::lsw::protocol::BufferReader&, VkPhysicalDeviceExternalFenceInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceExternalSemaphoreInfo(::lsw::protocol::Buffer&, const VkPhysicalDeviceExternalSemaphoreInfo&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceExternalSemaphoreInfo(::lsw::protocol::BufferReader&, VkPhysicalDeviceExternalSemaphoreInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceFeatures(::lsw::protocol::Buffer&, const VkPhysicalDeviceFeatures&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceFeatures(::lsw::protocol::BufferReader&, VkPhysicalDeviceFeatures*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceFeatures2(::lsw::protocol::Buffer&, const VkPhysicalDeviceFeatures2&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceFeatures2(::lsw::protocol::BufferReader&, VkPhysicalDeviceFeatures2*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceGroupProperties(::lsw::protocol::Buffer&, const VkPhysicalDeviceGroupProperties&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceGroupProperties(::lsw::protocol::BufferReader&, VkPhysicalDeviceGroupProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceImageFormatInfo2(::lsw::protocol::Buffer&, const VkPhysicalDeviceImageFormatInfo2&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceImageFormatInfo2(::lsw::protocol::BufferReader&, VkPhysicalDeviceImageFormatInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceLimits(::lsw::protocol::Buffer&, const VkPhysicalDeviceLimits&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceLimits(::lsw::protocol::BufferReader&, VkPhysicalDeviceLimits*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceMemoryProperties(::lsw::protocol::Buffer&, const VkPhysicalDeviceMemoryProperties&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceMemoryProperties(::lsw::protocol::BufferReader&, VkPhysicalDeviceMemoryProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceMemoryProperties2(::lsw::protocol::Buffer&, const VkPhysicalDeviceMemoryProperties2&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceMemoryProperties2(::lsw::protocol::BufferReader&, VkPhysicalDeviceMemoryProperties2*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceSparseProperties(::lsw::protocol::Buffer&, const VkPhysicalDeviceSparseProperties&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceSparseProperties(::lsw::protocol::BufferReader&, VkPhysicalDeviceSparseProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceProperties(::lsw::protocol::Buffer&, const VkPhysicalDeviceProperties&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceProperties(::lsw::protocol::BufferReader&, VkPhysicalDeviceProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceProperties2(::lsw::protocol::Buffer&, const VkPhysicalDeviceProperties2&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceProperties2(::lsw::protocol::BufferReader&, VkPhysicalDeviceProperties2*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceSparseImageFormatInfo2(::lsw::protocol::Buffer&, const VkPhysicalDeviceSparseImageFormatInfo2&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceSparseImageFormatInfo2(::lsw::protocol::BufferReader&, VkPhysicalDeviceSparseImageFormatInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkPhysicalDeviceToolProperties(::lsw::protocol::Buffer&, const VkPhysicalDeviceToolProperties&);
[[nodiscard]] inline bool deserialize_VkPhysicalDeviceToolProperties(::lsw::protocol::BufferReader&, VkPhysicalDeviceToolProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineCacheCreateInfo(::lsw::protocol::Buffer&, const VkPipelineCacheCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineCacheCreateInfo(::lsw::protocol::BufferReader&, VkPipelineCacheCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineColorBlendAttachmentState(::lsw::protocol::Buffer&, const VkPipelineColorBlendAttachmentState&);
[[nodiscard]] inline bool deserialize_VkPipelineColorBlendAttachmentState(::lsw::protocol::BufferReader&, VkPipelineColorBlendAttachmentState*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineColorBlendStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineColorBlendStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineColorBlendStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineColorBlendStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkStencilOpState(::lsw::protocol::Buffer&, const VkStencilOpState&);
[[nodiscard]] inline bool deserialize_VkStencilOpState(::lsw::protocol::BufferReader&, VkStencilOpState*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineDepthStencilStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineDepthStencilStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineDepthStencilStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineDepthStencilStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineDynamicStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineDynamicStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineDynamicStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineDynamicStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineInputAssemblyStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineInputAssemblyStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineInputAssemblyStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineInputAssemblyStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineLayoutCreateInfo(::lsw::protocol::Buffer&, const VkPipelineLayoutCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineLayoutCreateInfo(::lsw::protocol::BufferReader&, VkPipelineLayoutCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineMultisampleStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineMultisampleStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineMultisampleStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineMultisampleStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineRasterizationStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineRasterizationStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineRasterizationStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineRasterizationStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineTessellationStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineTessellationStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineTessellationStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineTessellationStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineVertexInputStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineVertexInputStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineVertexInputStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineVertexInputStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPipelineViewportStateCreateInfo(::lsw::protocol::Buffer&, const VkPipelineViewportStateCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPipelineViewportStateCreateInfo(::lsw::protocol::BufferReader&, VkPipelineViewportStateCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPresentInfoKHR(::lsw::protocol::Buffer&, const VkPresentInfoKHR&);
[[nodiscard]] inline bool deserialize_VkPresentInfoKHR(::lsw::protocol::BufferReader&, VkPresentInfoKHR*, ::lsw::protocol::Arena&);
inline void serialize_VkPrivateDataSlotCreateInfo(::lsw::protocol::Buffer&, const VkPrivateDataSlotCreateInfo&);
[[nodiscard]] inline bool deserialize_VkPrivateDataSlotCreateInfo(::lsw::protocol::BufferReader&, VkPrivateDataSlotCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkPushConstantRange(::lsw::protocol::Buffer&, const VkPushConstantRange&);
[[nodiscard]] inline bool deserialize_VkPushConstantRange(::lsw::protocol::BufferReader&, VkPushConstantRange*, ::lsw::protocol::Arena&);
inline void serialize_VkQueryPoolCreateInfo(::lsw::protocol::Buffer&, const VkQueryPoolCreateInfo&);
[[nodiscard]] inline bool deserialize_VkQueryPoolCreateInfo(::lsw::protocol::BufferReader&, VkQueryPoolCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkQueueFamilyProperties(::lsw::protocol::Buffer&, const VkQueueFamilyProperties&);
[[nodiscard]] inline bool deserialize_VkQueueFamilyProperties(::lsw::protocol::BufferReader&, VkQueueFamilyProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkQueueFamilyProperties2(::lsw::protocol::Buffer&, const VkQueueFamilyProperties2&);
[[nodiscard]] inline bool deserialize_VkQueueFamilyProperties2(::lsw::protocol::BufferReader&, VkQueueFamilyProperties2*, ::lsw::protocol::Arena&);
inline void serialize_VkRenderPassBeginInfo(::lsw::protocol::Buffer&, const VkRenderPassBeginInfo&);
[[nodiscard]] inline bool deserialize_VkRenderPassBeginInfo(::lsw::protocol::BufferReader&, VkRenderPassBeginInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkRenderPassCreateInfo(::lsw::protocol::Buffer&, const VkRenderPassCreateInfo&);
[[nodiscard]] inline bool deserialize_VkRenderPassCreateInfo(::lsw::protocol::BufferReader&, VkRenderPassCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkRenderPassCreateInfo2(::lsw::protocol::Buffer&, const VkRenderPassCreateInfo2&);
[[nodiscard]] inline bool deserialize_VkRenderPassCreateInfo2(::lsw::protocol::BufferReader&, VkRenderPassCreateInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkRenderingAttachmentInfo(::lsw::protocol::Buffer&, const VkRenderingAttachmentInfo&);
[[nodiscard]] inline bool deserialize_VkRenderingAttachmentInfo(::lsw::protocol::BufferReader&, VkRenderingAttachmentInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkRenderingInfo(::lsw::protocol::Buffer&, const VkRenderingInfo&);
[[nodiscard]] inline bool deserialize_VkRenderingInfo(::lsw::protocol::BufferReader&, VkRenderingInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkResolveImageInfo2(::lsw::protocol::Buffer&, const VkResolveImageInfo2&);
[[nodiscard]] inline bool deserialize_VkResolveImageInfo2(::lsw::protocol::BufferReader&, VkResolveImageInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkSamplerCreateInfo(::lsw::protocol::Buffer&, const VkSamplerCreateInfo&);
[[nodiscard]] inline bool deserialize_VkSamplerCreateInfo(::lsw::protocol::BufferReader&, VkSamplerCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSamplerYcbcrConversionCreateInfo(::lsw::protocol::Buffer&, const VkSamplerYcbcrConversionCreateInfo&);
[[nodiscard]] inline bool deserialize_VkSamplerYcbcrConversionCreateInfo(::lsw::protocol::BufferReader&, VkSamplerYcbcrConversionCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSemaphoreCreateInfo(::lsw::protocol::Buffer&, const VkSemaphoreCreateInfo&);
[[nodiscard]] inline bool deserialize_VkSemaphoreCreateInfo(::lsw::protocol::BufferReader&, VkSemaphoreCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSemaphoreSignalInfo(::lsw::protocol::Buffer&, const VkSemaphoreSignalInfo&);
[[nodiscard]] inline bool deserialize_VkSemaphoreSignalInfo(::lsw::protocol::BufferReader&, VkSemaphoreSignalInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSemaphoreSubmitInfo(::lsw::protocol::Buffer&, const VkSemaphoreSubmitInfo&);
[[nodiscard]] inline bool deserialize_VkSemaphoreSubmitInfo(::lsw::protocol::BufferReader&, VkSemaphoreSubmitInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSemaphoreWaitInfo(::lsw::protocol::Buffer&, const VkSemaphoreWaitInfo&);
[[nodiscard]] inline bool deserialize_VkSemaphoreWaitInfo(::lsw::protocol::BufferReader&, VkSemaphoreWaitInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkShaderModuleCreateInfo(::lsw::protocol::Buffer&, const VkShaderModuleCreateInfo&);
[[nodiscard]] inline bool deserialize_VkShaderModuleCreateInfo(::lsw::protocol::BufferReader&, VkShaderModuleCreateInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseBufferMemoryBindInfo(::lsw::protocol::Buffer&, const VkSparseBufferMemoryBindInfo&);
[[nodiscard]] inline bool deserialize_VkSparseBufferMemoryBindInfo(::lsw::protocol::BufferReader&, VkSparseBufferMemoryBindInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseImageFormatProperties(::lsw::protocol::Buffer&, const VkSparseImageFormatProperties&);
[[nodiscard]] inline bool deserialize_VkSparseImageFormatProperties(::lsw::protocol::BufferReader&, VkSparseImageFormatProperties*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseImageFormatProperties2(::lsw::protocol::Buffer&, const VkSparseImageFormatProperties2&);
[[nodiscard]] inline bool deserialize_VkSparseImageFormatProperties2(::lsw::protocol::BufferReader&, VkSparseImageFormatProperties2*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseImageMemoryBind(::lsw::protocol::Buffer&, const VkSparseImageMemoryBind&);
[[nodiscard]] inline bool deserialize_VkSparseImageMemoryBind(::lsw::protocol::BufferReader&, VkSparseImageMemoryBind*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseImageMemoryBindInfo(::lsw::protocol::Buffer&, const VkSparseImageMemoryBindInfo&);
[[nodiscard]] inline bool deserialize_VkSparseImageMemoryBindInfo(::lsw::protocol::BufferReader&, VkSparseImageMemoryBindInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseImageMemoryRequirements(::lsw::protocol::Buffer&, const VkSparseImageMemoryRequirements&);
[[nodiscard]] inline bool deserialize_VkSparseImageMemoryRequirements(::lsw::protocol::BufferReader&, VkSparseImageMemoryRequirements*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseImageMemoryRequirements2(::lsw::protocol::Buffer&, const VkSparseImageMemoryRequirements2&);
[[nodiscard]] inline bool deserialize_VkSparseImageMemoryRequirements2(::lsw::protocol::BufferReader&, VkSparseImageMemoryRequirements2*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseImageOpaqueMemoryBindInfo(::lsw::protocol::Buffer&, const VkSparseImageOpaqueMemoryBindInfo&);
[[nodiscard]] inline bool deserialize_VkSparseImageOpaqueMemoryBindInfo(::lsw::protocol::BufferReader&, VkSparseImageOpaqueMemoryBindInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSparseMemoryBind(::lsw::protocol::Buffer&, const VkSparseMemoryBind&);
[[nodiscard]] inline bool deserialize_VkSparseMemoryBind(::lsw::protocol::BufferReader&, VkSparseMemoryBind*, ::lsw::protocol::Arena&);
inline void serialize_VkSpecializationInfo(::lsw::protocol::Buffer&, const VkSpecializationInfo&);
[[nodiscard]] inline bool deserialize_VkSpecializationInfo(::lsw::protocol::BufferReader&, VkSpecializationInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSpecializationMapEntry(::lsw::protocol::Buffer&, const VkSpecializationMapEntry&);
[[nodiscard]] inline bool deserialize_VkSpecializationMapEntry(::lsw::protocol::BufferReader&, VkSpecializationMapEntry*, ::lsw::protocol::Arena&);
inline void serialize_VkSubmitInfo(::lsw::protocol::Buffer&, const VkSubmitInfo&);
[[nodiscard]] inline bool deserialize_VkSubmitInfo(::lsw::protocol::BufferReader&, VkSubmitInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSubmitInfo2(::lsw::protocol::Buffer&, const VkSubmitInfo2&);
[[nodiscard]] inline bool deserialize_VkSubmitInfo2(::lsw::protocol::BufferReader&, VkSubmitInfo2*, ::lsw::protocol::Arena&);
inline void serialize_VkSubpassBeginInfo(::lsw::protocol::Buffer&, const VkSubpassBeginInfo&);
[[nodiscard]] inline bool deserialize_VkSubpassBeginInfo(::lsw::protocol::BufferReader&, VkSubpassBeginInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSubpassDependency(::lsw::protocol::Buffer&, const VkSubpassDependency&);
[[nodiscard]] inline bool deserialize_VkSubpassDependency(::lsw::protocol::BufferReader&, VkSubpassDependency*, ::lsw::protocol::Arena&);
inline void serialize_VkSubpassDependency2(::lsw::protocol::Buffer&, const VkSubpassDependency2&);
[[nodiscard]] inline bool deserialize_VkSubpassDependency2(::lsw::protocol::BufferReader&, VkSubpassDependency2*, ::lsw::protocol::Arena&);
inline void serialize_VkSubpassDescription(::lsw::protocol::Buffer&, const VkSubpassDescription&);
[[nodiscard]] inline bool deserialize_VkSubpassDescription(::lsw::protocol::BufferReader&, VkSubpassDescription*, ::lsw::protocol::Arena&);
inline void serialize_VkSubpassDescription2(::lsw::protocol::Buffer&, const VkSubpassDescription2&);
[[nodiscard]] inline bool deserialize_VkSubpassDescription2(::lsw::protocol::BufferReader&, VkSubpassDescription2*, ::lsw::protocol::Arena&);
inline void serialize_VkSubpassEndInfo(::lsw::protocol::Buffer&, const VkSubpassEndInfo&);
[[nodiscard]] inline bool deserialize_VkSubpassEndInfo(::lsw::protocol::BufferReader&, VkSubpassEndInfo*, ::lsw::protocol::Arena&);
inline void serialize_VkSubresourceLayout(::lsw::protocol::Buffer&, const VkSubresourceLayout&);
[[nodiscard]] inline bool deserialize_VkSubresourceLayout(::lsw::protocol::BufferReader&, VkSubresourceLayout*, ::lsw::protocol::Arena&);
inline void serialize_VkSurfaceCapabilitiesKHR(::lsw::protocol::Buffer&, const VkSurfaceCapabilitiesKHR&);
[[nodiscard]] inline bool deserialize_VkSurfaceCapabilitiesKHR(::lsw::protocol::BufferReader&, VkSurfaceCapabilitiesKHR*, ::lsw::protocol::Arena&);
inline void serialize_VkSurfaceFormatKHR(::lsw::protocol::Buffer&, const VkSurfaceFormatKHR&);
[[nodiscard]] inline bool deserialize_VkSurfaceFormatKHR(::lsw::protocol::BufferReader&, VkSurfaceFormatKHR*, ::lsw::protocol::Arena&);
inline void serialize_VkSwapchainCreateInfoKHR(::lsw::protocol::Buffer&, const VkSwapchainCreateInfoKHR&);
[[nodiscard]] inline bool deserialize_VkSwapchainCreateInfoKHR(::lsw::protocol::BufferReader&, VkSwapchainCreateInfoKHR*, ::lsw::protocol::Arena&);
inline void serialize_VkVertexInputAttributeDescription(::lsw::protocol::Buffer&, const VkVertexInputAttributeDescription&);
[[nodiscard]] inline bool deserialize_VkVertexInputAttributeDescription(::lsw::protocol::BufferReader&, VkVertexInputAttributeDescription*, ::lsw::protocol::Arena&);
inline void serialize_VkVertexInputBindingDescription(::lsw::protocol::Buffer&, const VkVertexInputBindingDescription&);
[[nodiscard]] inline bool deserialize_VkVertexInputBindingDescription(::lsw::protocol::BufferReader&, VkVertexInputBindingDescription*, ::lsw::protocol::Arena&);
inline void serialize_VkViewport(::lsw::protocol::Buffer&, const VkViewport&);
[[nodiscard]] inline bool deserialize_VkViewport(::lsw::protocol::BufferReader&, VkViewport*, ::lsw::protocol::Arena&);
inline void serialize_VkWriteDescriptorSet(::lsw::protocol::Buffer&, const VkWriteDescriptorSet&);
[[nodiscard]] inline bool deserialize_VkWriteDescriptorSet(::lsw::protocol::BufferReader&, VkWriteDescriptorSet*, ::lsw::protocol::Arena&);

inline void serialize_VkAcquireNextImageInfoKHR(::lsw::protocol::Buffer& buf, const VkAcquireNextImageInfoKHR& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkSwapchainKHR(buf, v.swapchain);
    buf.write_pod(v.timeout);
    serialize_VkSemaphore(buf, v.semaphore);
    serialize_VkFence(buf, v.fence);
    buf.write_pod(v.deviceMask);
}

[[nodiscard]] inline bool deserialize_VkAcquireNextImageInfoKHR(::lsw::protocol::BufferReader& r, VkAcquireNextImageInfoKHR* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkSwapchainKHR(r, out->swapchain)) { return false; }
    if (!r.read_pod(out->timeout)) { return false; }
    if (!deserialize_VkSemaphore(r, out->semaphore)) { return false; }
    if (!deserialize_VkFence(r, out->fence)) { return false; }
    if (!r.read_pod(out->deviceMask)) { return false; }
    return true;
}

inline void serialize_VkApplicationInfo(::lsw::protocol::Buffer& buf, const VkApplicationInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    {
        const char* s = v.pApplicationName;
        if (s == nullptr) {
            buf.write_varint(0);
        } else {
            const std::size_t n = std::strlen(s) + 1u;
            buf.write_varint(n);
            buf.write(s, n);
        }
    }
    buf.write_pod(v.applicationVersion);
    {
        const char* s = v.pEngineName;
        if (s == nullptr) {
            buf.write_varint(0);
        } else {
            const std::size_t n = std::strlen(s) + 1u;
            buf.write_varint(n);
            buf.write(s, n);
        }
    }
    buf.write_pod(v.engineVersion);
    buf.write_pod(v.apiVersion);
}

[[nodiscard]] inline bool deserialize_VkApplicationInfo(::lsw::protocol::BufferReader& r, VkApplicationInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    {
        std::uint64_t n = 0;
        if (!r.read_varint(n)) { return false; }
        if (n == 0) {
            out->pApplicationName = nullptr;
        } else {
            char* s = arena.alloc_chars(static_cast<std::size_t>(n));
            if (!r.read(s, static_cast<std::size_t>(n))) { return false; }
            out->pApplicationName = s;
        }
    }
    if (!r.read_pod(out->applicationVersion)) { return false; }
    {
        std::uint64_t n = 0;
        if (!r.read_varint(n)) { return false; }
        if (n == 0) {
            out->pEngineName = nullptr;
        } else {
            char* s = arena.alloc_chars(static_cast<std::size_t>(n));
            if (!r.read(s, static_cast<std::size_t>(n))) { return false; }
            out->pEngineName = s;
        }
    }
    if (!r.read_pod(out->engineVersion)) { return false; }
    if (!r.read_pod(out->apiVersion)) { return false; }
    return true;
}

inline void serialize_VkAttachmentDescription(::lsw::protocol::Buffer& buf, const VkAttachmentDescription& v) {
    buf.write_pod(v.flags);
    buf.write_pod(v.format);
    buf.write_pod(v.samples);
    buf.write_pod(v.loadOp);
    buf.write_pod(v.storeOp);
    buf.write_pod(v.stencilLoadOp);
    buf.write_pod(v.stencilStoreOp);
    buf.write_pod(v.initialLayout);
    buf.write_pod(v.finalLayout);
}

[[nodiscard]] inline bool deserialize_VkAttachmentDescription(::lsw::protocol::BufferReader& r, VkAttachmentDescription* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->samples)) { return false; }
    if (!r.read_pod(out->loadOp)) { return false; }
    if (!r.read_pod(out->storeOp)) { return false; }
    if (!r.read_pod(out->stencilLoadOp)) { return false; }
    if (!r.read_pod(out->stencilStoreOp)) { return false; }
    if (!r.read_pod(out->initialLayout)) { return false; }
    if (!r.read_pod(out->finalLayout)) { return false; }
    return true;
}

inline void serialize_VkAttachmentDescription2(::lsw::protocol::Buffer& buf, const VkAttachmentDescription2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.format);
    buf.write_pod(v.samples);
    buf.write_pod(v.loadOp);
    buf.write_pod(v.storeOp);
    buf.write_pod(v.stencilLoadOp);
    buf.write_pod(v.stencilStoreOp);
    buf.write_pod(v.initialLayout);
    buf.write_pod(v.finalLayout);
}

[[nodiscard]] inline bool deserialize_VkAttachmentDescription2(::lsw::protocol::BufferReader& r, VkAttachmentDescription2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->samples)) { return false; }
    if (!r.read_pod(out->loadOp)) { return false; }
    if (!r.read_pod(out->storeOp)) { return false; }
    if (!r.read_pod(out->stencilLoadOp)) { return false; }
    if (!r.read_pod(out->stencilStoreOp)) { return false; }
    if (!r.read_pod(out->initialLayout)) { return false; }
    if (!r.read_pod(out->finalLayout)) { return false; }
    return true;
}

inline void serialize_VkAttachmentReference(::lsw::protocol::Buffer& buf, const VkAttachmentReference& v) {
    buf.write_pod(v.attachment);
    buf.write_pod(v.layout);
}

[[nodiscard]] inline bool deserialize_VkAttachmentReference(::lsw::protocol::BufferReader& r, VkAttachmentReference* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->attachment)) { return false; }
    if (!r.read_pod(out->layout)) { return false; }
    return true;
}

inline void serialize_VkAttachmentReference2(::lsw::protocol::Buffer& buf, const VkAttachmentReference2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.attachment);
    buf.write_pod(v.layout);
    buf.write_pod(v.aspectMask);
}

[[nodiscard]] inline bool deserialize_VkAttachmentReference2(::lsw::protocol::BufferReader& r, VkAttachmentReference2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->attachment)) { return false; }
    if (!r.read_pod(out->layout)) { return false; }
    if (!r.read_pod(out->aspectMask)) { return false; }
    return true;
}

inline void serialize_VkBindBufferMemoryInfo(::lsw::protocol::Buffer& buf, const VkBindBufferMemoryInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkBuffer(buf, v.buffer);
    serialize_VkDeviceMemory(buf, v.memory);
    buf.write_pod(v.memoryOffset);
}

[[nodiscard]] inline bool deserialize_VkBindBufferMemoryInfo(::lsw::protocol::BufferReader& r, VkBindBufferMemoryInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    if (!deserialize_VkDeviceMemory(r, out->memory)) { return false; }
    if (!r.read_pod(out->memoryOffset)) { return false; }
    return true;
}

inline void serialize_VkBindImageMemoryInfo(::lsw::protocol::Buffer& buf, const VkBindImageMemoryInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImage(buf, v.image);
    serialize_VkDeviceMemory(buf, v.memory);
    buf.write_pod(v.memoryOffset);
}

[[nodiscard]] inline bool deserialize_VkBindImageMemoryInfo(::lsw::protocol::BufferReader& r, VkBindImageMemoryInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImage(r, out->image)) { return false; }
    if (!deserialize_VkDeviceMemory(r, out->memory)) { return false; }
    if (!r.read_pod(out->memoryOffset)) { return false; }
    return true;
}

inline void serialize_VkBindSparseInfo(::lsw::protocol::Buffer& buf, const VkBindSparseInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.waitSemaphoreCount);
    {
        const std::uint8_t present = v.pWaitSemaphores != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.waitSemaphoreCount); ++i) {
                serialize_VkSemaphore(buf, v.pWaitSemaphores[i]);
            }
        }
    }
    buf.write_pod(v.bufferBindCount);
    {
        const std::uint8_t present = v.pBufferBinds != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.bufferBindCount); ++i) {
                serialize_VkSparseBufferMemoryBindInfo(buf, v.pBufferBinds[i]);
            }
        }
    }
    buf.write_pod(v.imageOpaqueBindCount);
    {
        const std::uint8_t present = v.pImageOpaqueBinds != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.imageOpaqueBindCount); ++i) {
                serialize_VkSparseImageOpaqueMemoryBindInfo(buf, v.pImageOpaqueBinds[i]);
            }
        }
    }
    buf.write_pod(v.imageBindCount);
    {
        const std::uint8_t present = v.pImageBinds != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.imageBindCount); ++i) {
                serialize_VkSparseImageMemoryBindInfo(buf, v.pImageBinds[i]);
            }
        }
    }
    buf.write_pod(v.signalSemaphoreCount);
    {
        const std::uint8_t present = v.pSignalSemaphores != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.signalSemaphoreCount); ++i) {
                serialize_VkSemaphore(buf, v.pSignalSemaphores[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkBindSparseInfo(::lsw::protocol::BufferReader& r, VkBindSparseInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->waitSemaphoreCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->waitSemaphoreCount == 0) {
            out->pWaitSemaphores = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphore>(static_cast<std::size_t>(out->waitSemaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->waitSemaphoreCount); ++i) {
                if (!deserialize_VkSemaphore(r, arr[i])) { return false; }
            }
            out->pWaitSemaphores = arr;
        }
    }
    if (!r.read_pod(out->bufferBindCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->bufferBindCount == 0) {
            out->pBufferBinds = nullptr;
        } else {
            auto* arr = arena.alloc<VkSparseBufferMemoryBindInfo>(static_cast<std::size_t>(out->bufferBindCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->bufferBindCount); ++i) {
                if (!deserialize_VkSparseBufferMemoryBindInfo(r, &arr[i], arena)) { return false; }
            }
            out->pBufferBinds = arr;
        }
    }
    if (!r.read_pod(out->imageOpaqueBindCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->imageOpaqueBindCount == 0) {
            out->pImageOpaqueBinds = nullptr;
        } else {
            auto* arr = arena.alloc<VkSparseImageOpaqueMemoryBindInfo>(static_cast<std::size_t>(out->imageOpaqueBindCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->imageOpaqueBindCount); ++i) {
                if (!deserialize_VkSparseImageOpaqueMemoryBindInfo(r, &arr[i], arena)) { return false; }
            }
            out->pImageOpaqueBinds = arr;
        }
    }
    if (!r.read_pod(out->imageBindCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->imageBindCount == 0) {
            out->pImageBinds = nullptr;
        } else {
            auto* arr = arena.alloc<VkSparseImageMemoryBindInfo>(static_cast<std::size_t>(out->imageBindCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->imageBindCount); ++i) {
                if (!deserialize_VkSparseImageMemoryBindInfo(r, &arr[i], arena)) { return false; }
            }
            out->pImageBinds = arr;
        }
    }
    if (!r.read_pod(out->signalSemaphoreCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->signalSemaphoreCount == 0) {
            out->pSignalSemaphores = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphore>(static_cast<std::size_t>(out->signalSemaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->signalSemaphoreCount); ++i) {
                if (!deserialize_VkSemaphore(r, arr[i])) { return false; }
            }
            out->pSignalSemaphores = arr;
        }
    }
    return true;
}

inline void serialize_VkBlitImageInfo2(::lsw::protocol::Buffer& buf, const VkBlitImageInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImage(buf, v.srcImage);
    buf.write_pod(v.srcImageLayout);
    serialize_VkImage(buf, v.dstImage);
    buf.write_pod(v.dstImageLayout);
    buf.write_pod(v.regionCount);
    {
        const std::uint8_t present = v.pRegions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.regionCount); ++i) {
                serialize_VkImageBlit2(buf, v.pRegions[i]);
            }
        }
    }
    buf.write_pod(v.filter);
}

[[nodiscard]] inline bool deserialize_VkBlitImageInfo2(::lsw::protocol::BufferReader& r, VkBlitImageInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImage(r, out->srcImage)) { return false; }
    if (!r.read_pod(out->srcImageLayout)) { return false; }
    if (!deserialize_VkImage(r, out->dstImage)) { return false; }
    if (!r.read_pod(out->dstImageLayout)) { return false; }
    if (!r.read_pod(out->regionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->regionCount == 0) {
            out->pRegions = nullptr;
        } else {
            auto* arr = arena.alloc<VkImageBlit2>(static_cast<std::size_t>(out->regionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->regionCount); ++i) {
                if (!deserialize_VkImageBlit2(r, &arr[i], arena)) { return false; }
            }
            out->pRegions = arr;
        }
    }
    if (!r.read_pod(out->filter)) { return false; }
    return true;
}

inline void serialize_VkBufferCopy(::lsw::protocol::Buffer& buf, const VkBufferCopy& v) {
    buf.write_pod(v.srcOffset);
    buf.write_pod(v.dstOffset);
    buf.write_pod(v.size);
}

[[nodiscard]] inline bool deserialize_VkBufferCopy(::lsw::protocol::BufferReader& r, VkBufferCopy* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->srcOffset)) { return false; }
    if (!r.read_pod(out->dstOffset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    return true;
}

inline void serialize_VkBufferCopy2(::lsw::protocol::Buffer& buf, const VkBufferCopy2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcOffset);
    buf.write_pod(v.dstOffset);
    buf.write_pod(v.size);
}

[[nodiscard]] inline bool deserialize_VkBufferCopy2(::lsw::protocol::BufferReader& r, VkBufferCopy2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcOffset)) { return false; }
    if (!r.read_pod(out->dstOffset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    return true;
}

inline void serialize_VkBufferCreateInfo(::lsw::protocol::Buffer& buf, const VkBufferCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.size);
    buf.write_pod(v.usage);
    buf.write_pod(v.sharingMode);
    buf.write_pod(v.queueFamilyIndexCount);
    {
        const std::uint8_t present = v.pQueueFamilyIndices != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.queueFamilyIndexCount); ++i) {
                buf.write_pod(v.pQueueFamilyIndices[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkBufferCreateInfo(::lsw::protocol::BufferReader& r, VkBufferCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    if (!r.read_pod(out->usage)) { return false; }
    if (!r.read_pod(out->sharingMode)) { return false; }
    if (!r.read_pod(out->queueFamilyIndexCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->queueFamilyIndexCount == 0) {
            out->pQueueFamilyIndices = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->queueFamilyIndexCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->queueFamilyIndexCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pQueueFamilyIndices = arr;
        }
    }
    return true;
}

inline void serialize_VkBufferDeviceAddressInfo(::lsw::protocol::Buffer& buf, const VkBufferDeviceAddressInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkBuffer(buf, v.buffer);
}

[[nodiscard]] inline bool deserialize_VkBufferDeviceAddressInfo(::lsw::protocol::BufferReader& r, VkBufferDeviceAddressInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    return true;
}

inline void serialize_VkImageSubresourceLayers(::lsw::protocol::Buffer& buf, const VkImageSubresourceLayers& v) {
    buf.write_pod(v.aspectMask);
    buf.write_pod(v.mipLevel);
    buf.write_pod(v.baseArrayLayer);
    buf.write_pod(v.layerCount);
}

[[nodiscard]] inline bool deserialize_VkImageSubresourceLayers(::lsw::protocol::BufferReader& r, VkImageSubresourceLayers* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->aspectMask)) { return false; }
    if (!r.read_pod(out->mipLevel)) { return false; }
    if (!r.read_pod(out->baseArrayLayer)) { return false; }
    if (!r.read_pod(out->layerCount)) { return false; }
    return true;
}

inline void serialize_VkOffset3D(::lsw::protocol::Buffer& buf, const VkOffset3D& v) {
    buf.write_pod(v.x);
    buf.write_pod(v.y);
    buf.write_pod(v.z);
}

[[nodiscard]] inline bool deserialize_VkOffset3D(::lsw::protocol::BufferReader& r, VkOffset3D* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->x)) { return false; }
    if (!r.read_pod(out->y)) { return false; }
    if (!r.read_pod(out->z)) { return false; }
    return true;
}

inline void serialize_VkExtent3D(::lsw::protocol::Buffer& buf, const VkExtent3D& v) {
    buf.write_pod(v.width);
    buf.write_pod(v.height);
    buf.write_pod(v.depth);
}

[[nodiscard]] inline bool deserialize_VkExtent3D(::lsw::protocol::BufferReader& r, VkExtent3D* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->width)) { return false; }
    if (!r.read_pod(out->height)) { return false; }
    if (!r.read_pod(out->depth)) { return false; }
    return true;
}

inline void serialize_VkBufferImageCopy(::lsw::protocol::Buffer& buf, const VkBufferImageCopy& v) {
    buf.write_pod(v.bufferOffset);
    buf.write_pod(v.bufferRowLength);
    buf.write_pod(v.bufferImageHeight);
    serialize_VkImageSubresourceLayers(buf, v.imageSubresource);
    serialize_VkOffset3D(buf, v.imageOffset);
    serialize_VkExtent3D(buf, v.imageExtent);
}

[[nodiscard]] inline bool deserialize_VkBufferImageCopy(::lsw::protocol::BufferReader& r, VkBufferImageCopy* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->bufferOffset)) { return false; }
    if (!r.read_pod(out->bufferRowLength)) { return false; }
    if (!r.read_pod(out->bufferImageHeight)) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->imageSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->imageOffset, arena)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->imageExtent, arena)) { return false; }
    return true;
}

inline void serialize_VkBufferImageCopy2(::lsw::protocol::Buffer& buf, const VkBufferImageCopy2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.bufferOffset);
    buf.write_pod(v.bufferRowLength);
    buf.write_pod(v.bufferImageHeight);
    serialize_VkImageSubresourceLayers(buf, v.imageSubresource);
    serialize_VkOffset3D(buf, v.imageOffset);
    serialize_VkExtent3D(buf, v.imageExtent);
}

[[nodiscard]] inline bool deserialize_VkBufferImageCopy2(::lsw::protocol::BufferReader& r, VkBufferImageCopy2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->bufferOffset)) { return false; }
    if (!r.read_pod(out->bufferRowLength)) { return false; }
    if (!r.read_pod(out->bufferImageHeight)) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->imageSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->imageOffset, arena)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->imageExtent, arena)) { return false; }
    return true;
}

inline void serialize_VkBufferMemoryBarrier(::lsw::protocol::Buffer& buf, const VkBufferMemoryBarrier& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstAccessMask);
    buf.write_pod(v.srcQueueFamilyIndex);
    buf.write_pod(v.dstQueueFamilyIndex);
    serialize_VkBuffer(buf, v.buffer);
    buf.write_pod(v.offset);
    buf.write_pod(v.size);
}

[[nodiscard]] inline bool deserialize_VkBufferMemoryBarrier(::lsw::protocol::BufferReader& r, VkBufferMemoryBarrier* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    if (!r.read_pod(out->srcQueueFamilyIndex)) { return false; }
    if (!r.read_pod(out->dstQueueFamilyIndex)) { return false; }
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    return true;
}

inline void serialize_VkBufferMemoryBarrier2(::lsw::protocol::Buffer& buf, const VkBufferMemoryBarrier2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcStageMask);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstStageMask);
    buf.write_pod(v.dstAccessMask);
    buf.write_pod(v.srcQueueFamilyIndex);
    buf.write_pod(v.dstQueueFamilyIndex);
    serialize_VkBuffer(buf, v.buffer);
    buf.write_pod(v.offset);
    buf.write_pod(v.size);
}

[[nodiscard]] inline bool deserialize_VkBufferMemoryBarrier2(::lsw::protocol::BufferReader& r, VkBufferMemoryBarrier2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcStageMask)) { return false; }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstStageMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    if (!r.read_pod(out->srcQueueFamilyIndex)) { return false; }
    if (!r.read_pod(out->dstQueueFamilyIndex)) { return false; }
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    return true;
}

inline void serialize_VkBufferMemoryRequirementsInfo2(::lsw::protocol::Buffer& buf, const VkBufferMemoryRequirementsInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkBuffer(buf, v.buffer);
}

[[nodiscard]] inline bool deserialize_VkBufferMemoryRequirementsInfo2(::lsw::protocol::BufferReader& r, VkBufferMemoryRequirementsInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    return true;
}

inline void serialize_VkBufferViewCreateInfo(::lsw::protocol::Buffer& buf, const VkBufferViewCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    serialize_VkBuffer(buf, v.buffer);
    buf.write_pod(v.format);
    buf.write_pod(v.offset);
    buf.write_pod(v.range);
}

[[nodiscard]] inline bool deserialize_VkBufferViewCreateInfo(::lsw::protocol::BufferReader& r, VkBufferViewCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->range)) { return false; }
    return true;
}

inline void serialize_VkClearColorValue(::lsw::protocol::Buffer& buf, const VkClearColorValue& v) {
    buf.write(&v, sizeof(v));
}

[[nodiscard]] inline bool deserialize_VkClearColorValue(::lsw::protocol::BufferReader& r, VkClearColorValue* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    return r.read(out, sizeof(*out));
}

inline void serialize_VkClearDepthStencilValue(::lsw::protocol::Buffer& buf, const VkClearDepthStencilValue& v) {
    buf.write_pod(v.depth);
    buf.write_pod(v.stencil);
}

[[nodiscard]] inline bool deserialize_VkClearDepthStencilValue(::lsw::protocol::BufferReader& r, VkClearDepthStencilValue* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->depth)) { return false; }
    if (!r.read_pod(out->stencil)) { return false; }
    return true;
}

inline void serialize_VkClearValue(::lsw::protocol::Buffer& buf, const VkClearValue& v) {
    buf.write(&v, sizeof(v));
}

[[nodiscard]] inline bool deserialize_VkClearValue(::lsw::protocol::BufferReader& r, VkClearValue* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    return r.read(out, sizeof(*out));
}

inline void serialize_VkClearAttachment(::lsw::protocol::Buffer& buf, const VkClearAttachment& v) {
    buf.write_pod(v.aspectMask);
    buf.write_pod(v.colorAttachment);
    serialize_VkClearValue(buf, v.clearValue);
}

[[nodiscard]] inline bool deserialize_VkClearAttachment(::lsw::protocol::BufferReader& r, VkClearAttachment* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->aspectMask)) { return false; }
    if (!r.read_pod(out->colorAttachment)) { return false; }
    if (!deserialize_VkClearValue(r, &out->clearValue, arena)) { return false; }
    return true;
}

inline void serialize_VkOffset2D(::lsw::protocol::Buffer& buf, const VkOffset2D& v) {
    buf.write_pod(v.x);
    buf.write_pod(v.y);
}

[[nodiscard]] inline bool deserialize_VkOffset2D(::lsw::protocol::BufferReader& r, VkOffset2D* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->x)) { return false; }
    if (!r.read_pod(out->y)) { return false; }
    return true;
}

inline void serialize_VkExtent2D(::lsw::protocol::Buffer& buf, const VkExtent2D& v) {
    buf.write_pod(v.width);
    buf.write_pod(v.height);
}

[[nodiscard]] inline bool deserialize_VkExtent2D(::lsw::protocol::BufferReader& r, VkExtent2D* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->width)) { return false; }
    if (!r.read_pod(out->height)) { return false; }
    return true;
}

inline void serialize_VkRect2D(::lsw::protocol::Buffer& buf, const VkRect2D& v) {
    serialize_VkOffset2D(buf, v.offset);
    serialize_VkExtent2D(buf, v.extent);
}

[[nodiscard]] inline bool deserialize_VkRect2D(::lsw::protocol::BufferReader& r, VkRect2D* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkOffset2D(r, &out->offset, arena)) { return false; }
    if (!deserialize_VkExtent2D(r, &out->extent, arena)) { return false; }
    return true;
}

inline void serialize_VkClearRect(::lsw::protocol::Buffer& buf, const VkClearRect& v) {
    serialize_VkRect2D(buf, v.rect);
    buf.write_pod(v.baseArrayLayer);
    buf.write_pod(v.layerCount);
}

[[nodiscard]] inline bool deserialize_VkClearRect(::lsw::protocol::BufferReader& r, VkClearRect* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkRect2D(r, &out->rect, arena)) { return false; }
    if (!r.read_pod(out->baseArrayLayer)) { return false; }
    if (!r.read_pod(out->layerCount)) { return false; }
    return true;
}

inline void serialize_VkCommandBufferAllocateInfo(::lsw::protocol::Buffer& buf, const VkCommandBufferAllocateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkCommandPool(buf, v.commandPool);
    buf.write_pod(v.level);
    buf.write_pod(v.commandBufferCount);
}

[[nodiscard]] inline bool deserialize_VkCommandBufferAllocateInfo(::lsw::protocol::BufferReader& r, VkCommandBufferAllocateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkCommandPool(r, out->commandPool)) { return false; }
    if (!r.read_pod(out->level)) { return false; }
    if (!r.read_pod(out->commandBufferCount)) { return false; }
    return true;
}

inline void serialize_VkCommandBufferBeginInfo(::lsw::protocol::Buffer& buf, const VkCommandBufferBeginInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    {
        const auto* p = v.pInheritanceInfo;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkCommandBufferInheritanceInfo(buf, *p);
        }
    }
}

[[nodiscard]] inline bool deserialize_VkCommandBufferBeginInfo(::lsw::protocol::BufferReader& r, VkCommandBufferBeginInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pInheritanceInfo = nullptr;
        } else {
            auto* p = arena.alloc<VkCommandBufferInheritanceInfo>();
            if (!deserialize_VkCommandBufferInheritanceInfo(r, p, arena)) { return false; }
            out->pInheritanceInfo = p;
        }
    }
    return true;
}

inline void serialize_VkCommandBufferInheritanceInfo(::lsw::protocol::Buffer& buf, const VkCommandBufferInheritanceInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkRenderPass(buf, v.renderPass);
    buf.write_pod(v.subpass);
    serialize_VkFramebuffer(buf, v.framebuffer);
    buf.write_pod(v.occlusionQueryEnable);
    buf.write_pod(v.queryFlags);
    buf.write_pod(v.pipelineStatistics);
}

[[nodiscard]] inline bool deserialize_VkCommandBufferInheritanceInfo(::lsw::protocol::BufferReader& r, VkCommandBufferInheritanceInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkRenderPass(r, out->renderPass)) { return false; }
    if (!r.read_pod(out->subpass)) { return false; }
    if (!deserialize_VkFramebuffer(r, out->framebuffer)) { return false; }
    if (!r.read_pod(out->occlusionQueryEnable)) { return false; }
    if (!r.read_pod(out->queryFlags)) { return false; }
    if (!r.read_pod(out->pipelineStatistics)) { return false; }
    return true;
}

inline void serialize_VkCommandBufferSubmitInfo(::lsw::protocol::Buffer& buf, const VkCommandBufferSubmitInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkCommandBuffer(buf, v.commandBuffer);
    buf.write_pod(v.deviceMask);
}

[[nodiscard]] inline bool deserialize_VkCommandBufferSubmitInfo(::lsw::protocol::BufferReader& r, VkCommandBufferSubmitInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkCommandBuffer(r, out->commandBuffer)) { return false; }
    if (!r.read_pod(out->deviceMask)) { return false; }
    return true;
}

inline void serialize_VkCommandPoolCreateInfo(::lsw::protocol::Buffer& buf, const VkCommandPoolCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.queueFamilyIndex);
}

[[nodiscard]] inline bool deserialize_VkCommandPoolCreateInfo(::lsw::protocol::BufferReader& r, VkCommandPoolCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->queueFamilyIndex)) { return false; }
    return true;
}

inline void serialize_VkComponentMapping(::lsw::protocol::Buffer& buf, const VkComponentMapping& v) {
    buf.write_pod(v.r);
    buf.write_pod(v.g);
    buf.write_pod(v.b);
    buf.write_pod(v.a);
}

[[nodiscard]] inline bool deserialize_VkComponentMapping(::lsw::protocol::BufferReader& r, VkComponentMapping* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->r)) { return false; }
    if (!r.read_pod(out->g)) { return false; }
    if (!r.read_pod(out->b)) { return false; }
    if (!r.read_pod(out->a)) { return false; }
    return true;
}

inline void serialize_VkPipelineShaderStageCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineShaderStageCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.stage);
    serialize_VkShaderModule(buf, v.module);
    {
        const char* s = v.pName;
        if (s == nullptr) {
            buf.write_varint(0);
        } else {
            const std::size_t n = std::strlen(s) + 1u;
            buf.write_varint(n);
            buf.write(s, n);
        }
    }
    {
        const auto* p = v.pSpecializationInfo;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkSpecializationInfo(buf, *p);
        }
    }
}

[[nodiscard]] inline bool deserialize_VkPipelineShaderStageCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineShaderStageCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->stage)) { return false; }
    if (!deserialize_VkShaderModule(r, out->module)) { return false; }
    {
        std::uint64_t n = 0;
        if (!r.read_varint(n)) { return false; }
        if (n == 0) {
            out->pName = nullptr;
        } else {
            char* s = arena.alloc_chars(static_cast<std::size_t>(n));
            if (!r.read(s, static_cast<std::size_t>(n))) { return false; }
            out->pName = s;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pSpecializationInfo = nullptr;
        } else {
            auto* p = arena.alloc<VkSpecializationInfo>();
            if (!deserialize_VkSpecializationInfo(r, p, arena)) { return false; }
            out->pSpecializationInfo = p;
        }
    }
    return true;
}

inline void serialize_VkComputePipelineCreateInfo(::lsw::protocol::Buffer& buf, const VkComputePipelineCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    serialize_VkPipelineShaderStageCreateInfo(buf, v.stage);
    serialize_VkPipelineLayout(buf, v.layout);
    serialize_VkPipeline(buf, v.basePipelineHandle);
    buf.write_pod(v.basePipelineIndex);
}

[[nodiscard]] inline bool deserialize_VkComputePipelineCreateInfo(::lsw::protocol::BufferReader& r, VkComputePipelineCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!deserialize_VkPipelineShaderStageCreateInfo(r, &out->stage, arena)) { return false; }
    if (!deserialize_VkPipelineLayout(r, out->layout)) { return false; }
    if (!deserialize_VkPipeline(r, out->basePipelineHandle)) { return false; }
    if (!r.read_pod(out->basePipelineIndex)) { return false; }
    return true;
}

inline void serialize_VkCopyBufferInfo2(::lsw::protocol::Buffer& buf, const VkCopyBufferInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkBuffer(buf, v.srcBuffer);
    serialize_VkBuffer(buf, v.dstBuffer);
    buf.write_pod(v.regionCount);
    {
        const std::uint8_t present = v.pRegions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.regionCount); ++i) {
                serialize_VkBufferCopy2(buf, v.pRegions[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkCopyBufferInfo2(::lsw::protocol::BufferReader& r, VkCopyBufferInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkBuffer(r, out->srcBuffer)) { return false; }
    if (!deserialize_VkBuffer(r, out->dstBuffer)) { return false; }
    if (!r.read_pod(out->regionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->regionCount == 0) {
            out->pRegions = nullptr;
        } else {
            auto* arr = arena.alloc<VkBufferCopy2>(static_cast<std::size_t>(out->regionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->regionCount); ++i) {
                if (!deserialize_VkBufferCopy2(r, &arr[i], arena)) { return false; }
            }
            out->pRegions = arr;
        }
    }
    return true;
}

inline void serialize_VkCopyBufferToImageInfo2(::lsw::protocol::Buffer& buf, const VkCopyBufferToImageInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkBuffer(buf, v.srcBuffer);
    serialize_VkImage(buf, v.dstImage);
    buf.write_pod(v.dstImageLayout);
    buf.write_pod(v.regionCount);
    {
        const std::uint8_t present = v.pRegions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.regionCount); ++i) {
                serialize_VkBufferImageCopy2(buf, v.pRegions[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkCopyBufferToImageInfo2(::lsw::protocol::BufferReader& r, VkCopyBufferToImageInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkBuffer(r, out->srcBuffer)) { return false; }
    if (!deserialize_VkImage(r, out->dstImage)) { return false; }
    if (!r.read_pod(out->dstImageLayout)) { return false; }
    if (!r.read_pod(out->regionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->regionCount == 0) {
            out->pRegions = nullptr;
        } else {
            auto* arr = arena.alloc<VkBufferImageCopy2>(static_cast<std::size_t>(out->regionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->regionCount); ++i) {
                if (!deserialize_VkBufferImageCopy2(r, &arr[i], arena)) { return false; }
            }
            out->pRegions = arr;
        }
    }
    return true;
}

inline void serialize_VkCopyDescriptorSet(::lsw::protocol::Buffer& buf, const VkCopyDescriptorSet& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkDescriptorSet(buf, v.srcSet);
    buf.write_pod(v.srcBinding);
    buf.write_pod(v.srcArrayElement);
    serialize_VkDescriptorSet(buf, v.dstSet);
    buf.write_pod(v.dstBinding);
    buf.write_pod(v.dstArrayElement);
    buf.write_pod(v.descriptorCount);
}

[[nodiscard]] inline bool deserialize_VkCopyDescriptorSet(::lsw::protocol::BufferReader& r, VkCopyDescriptorSet* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkDescriptorSet(r, out->srcSet)) { return false; }
    if (!r.read_pod(out->srcBinding)) { return false; }
    if (!r.read_pod(out->srcArrayElement)) { return false; }
    if (!deserialize_VkDescriptorSet(r, out->dstSet)) { return false; }
    if (!r.read_pod(out->dstBinding)) { return false; }
    if (!r.read_pod(out->dstArrayElement)) { return false; }
    if (!r.read_pod(out->descriptorCount)) { return false; }
    return true;
}

inline void serialize_VkCopyImageInfo2(::lsw::protocol::Buffer& buf, const VkCopyImageInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImage(buf, v.srcImage);
    buf.write_pod(v.srcImageLayout);
    serialize_VkImage(buf, v.dstImage);
    buf.write_pod(v.dstImageLayout);
    buf.write_pod(v.regionCount);
    {
        const std::uint8_t present = v.pRegions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.regionCount); ++i) {
                serialize_VkImageCopy2(buf, v.pRegions[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkCopyImageInfo2(::lsw::protocol::BufferReader& r, VkCopyImageInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImage(r, out->srcImage)) { return false; }
    if (!r.read_pod(out->srcImageLayout)) { return false; }
    if (!deserialize_VkImage(r, out->dstImage)) { return false; }
    if (!r.read_pod(out->dstImageLayout)) { return false; }
    if (!r.read_pod(out->regionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->regionCount == 0) {
            out->pRegions = nullptr;
        } else {
            auto* arr = arena.alloc<VkImageCopy2>(static_cast<std::size_t>(out->regionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->regionCount); ++i) {
                if (!deserialize_VkImageCopy2(r, &arr[i], arena)) { return false; }
            }
            out->pRegions = arr;
        }
    }
    return true;
}

inline void serialize_VkCopyImageToBufferInfo2(::lsw::protocol::Buffer& buf, const VkCopyImageToBufferInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImage(buf, v.srcImage);
    buf.write_pod(v.srcImageLayout);
    serialize_VkBuffer(buf, v.dstBuffer);
    buf.write_pod(v.regionCount);
    {
        const std::uint8_t present = v.pRegions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.regionCount); ++i) {
                serialize_VkBufferImageCopy2(buf, v.pRegions[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkCopyImageToBufferInfo2(::lsw::protocol::BufferReader& r, VkCopyImageToBufferInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImage(r, out->srcImage)) { return false; }
    if (!r.read_pod(out->srcImageLayout)) { return false; }
    if (!deserialize_VkBuffer(r, out->dstBuffer)) { return false; }
    if (!r.read_pod(out->regionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->regionCount == 0) {
            out->pRegions = nullptr;
        } else {
            auto* arr = arena.alloc<VkBufferImageCopy2>(static_cast<std::size_t>(out->regionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->regionCount); ++i) {
                if (!deserialize_VkBufferImageCopy2(r, &arr[i], arena)) { return false; }
            }
            out->pRegions = arr;
        }
    }
    return true;
}

inline void serialize_VkDependencyInfo(::lsw::protocol::Buffer& buf, const VkDependencyInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.dependencyFlags);
    buf.write_pod(v.memoryBarrierCount);
    {
        const std::uint8_t present = v.pMemoryBarriers != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.memoryBarrierCount); ++i) {
                serialize_VkMemoryBarrier2(buf, v.pMemoryBarriers[i]);
            }
        }
    }
    buf.write_pod(v.bufferMemoryBarrierCount);
    {
        const std::uint8_t present = v.pBufferMemoryBarriers != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.bufferMemoryBarrierCount); ++i) {
                serialize_VkBufferMemoryBarrier2(buf, v.pBufferMemoryBarriers[i]);
            }
        }
    }
    buf.write_pod(v.imageMemoryBarrierCount);
    {
        const std::uint8_t present = v.pImageMemoryBarriers != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.imageMemoryBarrierCount); ++i) {
                serialize_VkImageMemoryBarrier2(buf, v.pImageMemoryBarriers[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDependencyInfo(::lsw::protocol::BufferReader& r, VkDependencyInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->dependencyFlags)) { return false; }
    if (!r.read_pod(out->memoryBarrierCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->memoryBarrierCount == 0) {
            out->pMemoryBarriers = nullptr;
        } else {
            auto* arr = arena.alloc<VkMemoryBarrier2>(static_cast<std::size_t>(out->memoryBarrierCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->memoryBarrierCount); ++i) {
                if (!deserialize_VkMemoryBarrier2(r, &arr[i], arena)) { return false; }
            }
            out->pMemoryBarriers = arr;
        }
    }
    if (!r.read_pod(out->bufferMemoryBarrierCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->bufferMemoryBarrierCount == 0) {
            out->pBufferMemoryBarriers = nullptr;
        } else {
            auto* arr = arena.alloc<VkBufferMemoryBarrier2>(static_cast<std::size_t>(out->bufferMemoryBarrierCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->bufferMemoryBarrierCount); ++i) {
                if (!deserialize_VkBufferMemoryBarrier2(r, &arr[i], arena)) { return false; }
            }
            out->pBufferMemoryBarriers = arr;
        }
    }
    if (!r.read_pod(out->imageMemoryBarrierCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->imageMemoryBarrierCount == 0) {
            out->pImageMemoryBarriers = nullptr;
        } else {
            auto* arr = arena.alloc<VkImageMemoryBarrier2>(static_cast<std::size_t>(out->imageMemoryBarrierCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->imageMemoryBarrierCount); ++i) {
                if (!deserialize_VkImageMemoryBarrier2(r, &arr[i], arena)) { return false; }
            }
            out->pImageMemoryBarriers = arr;
        }
    }
    return true;
}

inline void serialize_VkDescriptorBufferInfo(::lsw::protocol::Buffer& buf, const VkDescriptorBufferInfo& v) {
    serialize_VkBuffer(buf, v.buffer);
    buf.write_pod(v.offset);
    buf.write_pod(v.range);
}

[[nodiscard]] inline bool deserialize_VkDescriptorBufferInfo(::lsw::protocol::BufferReader& r, VkDescriptorBufferInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->range)) { return false; }
    return true;
}

inline void serialize_VkDescriptorImageInfo(::lsw::protocol::Buffer& buf, const VkDescriptorImageInfo& v) {
    serialize_VkSampler(buf, v.sampler);
    serialize_VkImageView(buf, v.imageView);
    buf.write_pod(v.imageLayout);
}

[[nodiscard]] inline bool deserialize_VkDescriptorImageInfo(::lsw::protocol::BufferReader& r, VkDescriptorImageInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkSampler(r, out->sampler)) { return false; }
    if (!deserialize_VkImageView(r, out->imageView)) { return false; }
    if (!r.read_pod(out->imageLayout)) { return false; }
    return true;
}

inline void serialize_VkDescriptorPoolCreateInfo(::lsw::protocol::Buffer& buf, const VkDescriptorPoolCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.maxSets);
    buf.write_pod(v.poolSizeCount);
    {
        const std::uint8_t present = v.pPoolSizes != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.poolSizeCount); ++i) {
                serialize_VkDescriptorPoolSize(buf, v.pPoolSizes[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDescriptorPoolCreateInfo(::lsw::protocol::BufferReader& r, VkDescriptorPoolCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->maxSets)) { return false; }
    if (!r.read_pod(out->poolSizeCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->poolSizeCount == 0) {
            out->pPoolSizes = nullptr;
        } else {
            auto* arr = arena.alloc<VkDescriptorPoolSize>(static_cast<std::size_t>(out->poolSizeCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->poolSizeCount); ++i) {
                if (!deserialize_VkDescriptorPoolSize(r, &arr[i], arena)) { return false; }
            }
            out->pPoolSizes = arr;
        }
    }
    return true;
}

inline void serialize_VkDescriptorPoolSize(::lsw::protocol::Buffer& buf, const VkDescriptorPoolSize& v) {
    buf.write_pod(v.type);
    buf.write_pod(v.descriptorCount);
}

[[nodiscard]] inline bool deserialize_VkDescriptorPoolSize(::lsw::protocol::BufferReader& r, VkDescriptorPoolSize* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->type)) { return false; }
    if (!r.read_pod(out->descriptorCount)) { return false; }
    return true;
}

inline void serialize_VkDescriptorSetAllocateInfo(::lsw::protocol::Buffer& buf, const VkDescriptorSetAllocateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkDescriptorPool(buf, v.descriptorPool);
    buf.write_pod(v.descriptorSetCount);
    {
        const std::uint8_t present = v.pSetLayouts != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.descriptorSetCount); ++i) {
                serialize_VkDescriptorSetLayout(buf, v.pSetLayouts[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDescriptorSetAllocateInfo(::lsw::protocol::BufferReader& r, VkDescriptorSetAllocateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkDescriptorPool(r, out->descriptorPool)) { return false; }
    if (!r.read_pod(out->descriptorSetCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->descriptorSetCount == 0) {
            out->pSetLayouts = nullptr;
        } else {
            auto* arr = arena.alloc<VkDescriptorSetLayout>(static_cast<std::size_t>(out->descriptorSetCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->descriptorSetCount); ++i) {
                if (!deserialize_VkDescriptorSetLayout(r, arr[i])) { return false; }
            }
            out->pSetLayouts = arr;
        }
    }
    return true;
}

inline void serialize_VkDescriptorSetLayoutBinding(::lsw::protocol::Buffer& buf, const VkDescriptorSetLayoutBinding& v) {
    buf.write_pod(v.binding);
    buf.write_pod(v.descriptorType);
    buf.write_pod(v.descriptorCount);
    buf.write_pod(v.stageFlags);
    {
        const std::uint8_t present = v.pImmutableSamplers != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.descriptorCount); ++i) {
                serialize_VkSampler(buf, v.pImmutableSamplers[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDescriptorSetLayoutBinding(::lsw::protocol::BufferReader& r, VkDescriptorSetLayoutBinding* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->binding)) { return false; }
    if (!r.read_pod(out->descriptorType)) { return false; }
    if (!r.read_pod(out->descriptorCount)) { return false; }
    if (!r.read_pod(out->stageFlags)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->descriptorCount == 0) {
            out->pImmutableSamplers = nullptr;
        } else {
            auto* arr = arena.alloc<VkSampler>(static_cast<std::size_t>(out->descriptorCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->descriptorCount); ++i) {
                if (!deserialize_VkSampler(r, arr[i])) { return false; }
            }
            out->pImmutableSamplers = arr;
        }
    }
    return true;
}

inline void serialize_VkDescriptorSetLayoutCreateInfo(::lsw::protocol::Buffer& buf, const VkDescriptorSetLayoutCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.bindingCount);
    {
        const std::uint8_t present = v.pBindings != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.bindingCount); ++i) {
                serialize_VkDescriptorSetLayoutBinding(buf, v.pBindings[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDescriptorSetLayoutCreateInfo(::lsw::protocol::BufferReader& r, VkDescriptorSetLayoutCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->bindingCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->bindingCount == 0) {
            out->pBindings = nullptr;
        } else {
            auto* arr = arena.alloc<VkDescriptorSetLayoutBinding>(static_cast<std::size_t>(out->bindingCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->bindingCount); ++i) {
                if (!deserialize_VkDescriptorSetLayoutBinding(r, &arr[i], arena)) { return false; }
            }
            out->pBindings = arr;
        }
    }
    return true;
}

inline void serialize_VkDescriptorSetLayoutSupport(::lsw::protocol::Buffer& buf, const VkDescriptorSetLayoutSupport& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.supported);
}

[[nodiscard]] inline bool deserialize_VkDescriptorSetLayoutSupport(::lsw::protocol::BufferReader& r, VkDescriptorSetLayoutSupport* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->supported)) { return false; }
    return true;
}

inline void serialize_VkDescriptorUpdateTemplateCreateInfo(::lsw::protocol::Buffer& buf, const VkDescriptorUpdateTemplateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.descriptorUpdateEntryCount);
    {
        const std::uint8_t present = v.pDescriptorUpdateEntries != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.descriptorUpdateEntryCount); ++i) {
                serialize_VkDescriptorUpdateTemplateEntry(buf, v.pDescriptorUpdateEntries[i]);
            }
        }
    }
    buf.write_pod(v.templateType);
    serialize_VkDescriptorSetLayout(buf, v.descriptorSetLayout);
    buf.write_pod(v.pipelineBindPoint);
    serialize_VkPipelineLayout(buf, v.pipelineLayout);
    buf.write_pod(v.set);
}

[[nodiscard]] inline bool deserialize_VkDescriptorUpdateTemplateCreateInfo(::lsw::protocol::BufferReader& r, VkDescriptorUpdateTemplateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->descriptorUpdateEntryCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->descriptorUpdateEntryCount == 0) {
            out->pDescriptorUpdateEntries = nullptr;
        } else {
            auto* arr = arena.alloc<VkDescriptorUpdateTemplateEntry>(static_cast<std::size_t>(out->descriptorUpdateEntryCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->descriptorUpdateEntryCount); ++i) {
                if (!deserialize_VkDescriptorUpdateTemplateEntry(r, &arr[i], arena)) { return false; }
            }
            out->pDescriptorUpdateEntries = arr;
        }
    }
    if (!r.read_pod(out->templateType)) { return false; }
    if (!deserialize_VkDescriptorSetLayout(r, out->descriptorSetLayout)) { return false; }
    if (!r.read_pod(out->pipelineBindPoint)) { return false; }
    if (!deserialize_VkPipelineLayout(r, out->pipelineLayout)) { return false; }
    if (!r.read_pod(out->set)) { return false; }
    return true;
}

inline void serialize_VkDescriptorUpdateTemplateEntry(::lsw::protocol::Buffer& buf, const VkDescriptorUpdateTemplateEntry& v) {
    buf.write_pod(v.dstBinding);
    buf.write_pod(v.dstArrayElement);
    buf.write_pod(v.descriptorCount);
    buf.write_pod(v.descriptorType);
    buf.write_pod(v.offset);
    buf.write_pod(v.stride);
}

[[nodiscard]] inline bool deserialize_VkDescriptorUpdateTemplateEntry(::lsw::protocol::BufferReader& r, VkDescriptorUpdateTemplateEntry* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->dstBinding)) { return false; }
    if (!r.read_pod(out->dstArrayElement)) { return false; }
    if (!r.read_pod(out->descriptorCount)) { return false; }
    if (!r.read_pod(out->descriptorType)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->stride)) { return false; }
    return true;
}

inline void serialize_VkDeviceBufferMemoryRequirements(::lsw::protocol::Buffer& buf, const VkDeviceBufferMemoryRequirements& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    {
        const auto* p = v.pCreateInfo;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkBufferCreateInfo(buf, *p);
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDeviceBufferMemoryRequirements(::lsw::protocol::BufferReader& r, VkDeviceBufferMemoryRequirements* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pCreateInfo = nullptr;
        } else {
            auto* p = arena.alloc<VkBufferCreateInfo>();
            if (!deserialize_VkBufferCreateInfo(r, p, arena)) { return false; }
            out->pCreateInfo = p;
        }
    }
    return true;
}

inline void serialize_VkDeviceCreateInfo(::lsw::protocol::Buffer& buf, const VkDeviceCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.queueCreateInfoCount);
    {
        const std::uint8_t present = v.pQueueCreateInfos != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.queueCreateInfoCount); ++i) {
                serialize_VkDeviceQueueCreateInfo(buf, v.pQueueCreateInfos[i]);
            }
        }
    }
    buf.write_pod(v.enabledLayerCount);
    {
        const std::uint8_t present = v.ppEnabledLayerNames != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.enabledLayerCount); ++i) {
                const char* s = v.ppEnabledLayerNames[i];
                if (s == nullptr) {
                    buf.write_varint(0);
                } else {
                    const std::size_t n = std::strlen(s) + 1u;
                    buf.write_varint(n);
                    buf.write(s, n);
                }
            }
        }
    }
    buf.write_pod(v.enabledExtensionCount);
    {
        const std::uint8_t present = v.ppEnabledExtensionNames != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.enabledExtensionCount); ++i) {
                const char* s = v.ppEnabledExtensionNames[i];
                if (s == nullptr) {
                    buf.write_varint(0);
                } else {
                    const std::size_t n = std::strlen(s) + 1u;
                    buf.write_varint(n);
                    buf.write(s, n);
                }
            }
        }
    }
    {
        const auto* p = v.pEnabledFeatures;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPhysicalDeviceFeatures(buf, *p);
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDeviceCreateInfo(::lsw::protocol::BufferReader& r, VkDeviceCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->queueCreateInfoCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->queueCreateInfoCount == 0) {
            out->pQueueCreateInfos = nullptr;
        } else {
            auto* arr = arena.alloc<VkDeviceQueueCreateInfo>(static_cast<std::size_t>(out->queueCreateInfoCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->queueCreateInfoCount); ++i) {
                if (!deserialize_VkDeviceQueueCreateInfo(r, &arr[i], arena)) { return false; }
            }
            out->pQueueCreateInfos = arr;
        }
    }
    if (!r.read_pod(out->enabledLayerCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->enabledLayerCount == 0) {
            out->ppEnabledLayerNames = nullptr;
        } else {
            auto** arr = arena.alloc<const char*>(static_cast<std::size_t>(out->enabledLayerCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->enabledLayerCount); ++i) {
                std::uint64_t n = 0;
                if (!r.read_varint(n)) { return false; }
                if (n == 0) {
                    arr[i] = nullptr;
                } else {
                    char* s = arena.alloc_chars(static_cast<std::size_t>(n));
                    if (!r.read(s, static_cast<std::size_t>(n))) { return false; }
                    arr[i] = s;
                }
            }
            out->ppEnabledLayerNames = arr;
        }
    }
    if (!r.read_pod(out->enabledExtensionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->enabledExtensionCount == 0) {
            out->ppEnabledExtensionNames = nullptr;
        } else {
            auto** arr = arena.alloc<const char*>(static_cast<std::size_t>(out->enabledExtensionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->enabledExtensionCount); ++i) {
                std::uint64_t n = 0;
                if (!r.read_varint(n)) { return false; }
                if (n == 0) {
                    arr[i] = nullptr;
                } else {
                    char* s = arena.alloc_chars(static_cast<std::size_t>(n));
                    if (!r.read(s, static_cast<std::size_t>(n))) { return false; }
                    arr[i] = s;
                }
            }
            out->ppEnabledExtensionNames = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pEnabledFeatures = nullptr;
        } else {
            auto* p = arena.alloc<VkPhysicalDeviceFeatures>();
            if (!deserialize_VkPhysicalDeviceFeatures(r, p, arena)) { return false; }
            out->pEnabledFeatures = p;
        }
    }
    return true;
}

inline void serialize_VkDeviceGroupPresentCapabilitiesKHR(::lsw::protocol::Buffer& buf, const VkDeviceGroupPresentCapabilitiesKHR& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write(&v.presentMask[0], sizeof(v.presentMask));
    buf.write_pod(v.modes);
}

[[nodiscard]] inline bool deserialize_VkDeviceGroupPresentCapabilitiesKHR(::lsw::protocol::BufferReader& r, VkDeviceGroupPresentCapabilitiesKHR* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read(&out->presentMask[0], sizeof(out->presentMask))) { return false; }
    if (!r.read_pod(out->modes)) { return false; }
    return true;
}

inline void serialize_VkDeviceImageMemoryRequirements(::lsw::protocol::Buffer& buf, const VkDeviceImageMemoryRequirements& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    {
        const auto* p = v.pCreateInfo;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkImageCreateInfo(buf, *p);
        }
    }
    buf.write_pod(v.planeAspect);
}

[[nodiscard]] inline bool deserialize_VkDeviceImageMemoryRequirements(::lsw::protocol::BufferReader& r, VkDeviceImageMemoryRequirements* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pCreateInfo = nullptr;
        } else {
            auto* p = arena.alloc<VkImageCreateInfo>();
            if (!deserialize_VkImageCreateInfo(r, p, arena)) { return false; }
            out->pCreateInfo = p;
        }
    }
    if (!r.read_pod(out->planeAspect)) { return false; }
    return true;
}

inline void serialize_VkDeviceMemoryOpaqueCaptureAddressInfo(::lsw::protocol::Buffer& buf, const VkDeviceMemoryOpaqueCaptureAddressInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkDeviceMemory(buf, v.memory);
}

[[nodiscard]] inline bool deserialize_VkDeviceMemoryOpaqueCaptureAddressInfo(::lsw::protocol::BufferReader& r, VkDeviceMemoryOpaqueCaptureAddressInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkDeviceMemory(r, out->memory)) { return false; }
    return true;
}

inline void serialize_VkDeviceQueueCreateInfo(::lsw::protocol::Buffer& buf, const VkDeviceQueueCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.queueFamilyIndex);
    buf.write_pod(v.queueCount);
    {
        const std::uint8_t present = v.pQueuePriorities != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.queueCount); ++i) {
                buf.write_pod(v.pQueuePriorities[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkDeviceQueueCreateInfo(::lsw::protocol::BufferReader& r, VkDeviceQueueCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->queueFamilyIndex)) { return false; }
    if (!r.read_pod(out->queueCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->queueCount == 0) {
            out->pQueuePriorities = nullptr;
        } else {
            auto* arr = arena.alloc<float>(static_cast<std::size_t>(out->queueCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->queueCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pQueuePriorities = arr;
        }
    }
    return true;
}

inline void serialize_VkDeviceQueueInfo2(::lsw::protocol::Buffer& buf, const VkDeviceQueueInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.queueFamilyIndex);
    buf.write_pod(v.queueIndex);
}

[[nodiscard]] inline bool deserialize_VkDeviceQueueInfo2(::lsw::protocol::BufferReader& r, VkDeviceQueueInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->queueFamilyIndex)) { return false; }
    if (!r.read_pod(out->queueIndex)) { return false; }
    return true;
}

inline void serialize_VkEventCreateInfo(::lsw::protocol::Buffer& buf, const VkEventCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkEventCreateInfo(::lsw::protocol::BufferReader& r, VkEventCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkExtensionProperties(::lsw::protocol::Buffer& buf, const VkExtensionProperties& v) {
    buf.write(&v.extensionName[0], sizeof(v.extensionName));
    buf.write_pod(v.specVersion);
}

[[nodiscard]] inline bool deserialize_VkExtensionProperties(::lsw::protocol::BufferReader& r, VkExtensionProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read(&out->extensionName[0], sizeof(out->extensionName))) { return false; }
    if (!r.read_pod(out->specVersion)) { return false; }
    return true;
}

inline void serialize_VkExternalMemoryProperties(::lsw::protocol::Buffer& buf, const VkExternalMemoryProperties& v) {
    buf.write_pod(v.externalMemoryFeatures);
    buf.write_pod(v.exportFromImportedHandleTypes);
    buf.write_pod(v.compatibleHandleTypes);
}

[[nodiscard]] inline bool deserialize_VkExternalMemoryProperties(::lsw::protocol::BufferReader& r, VkExternalMemoryProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->externalMemoryFeatures)) { return false; }
    if (!r.read_pod(out->exportFromImportedHandleTypes)) { return false; }
    if (!r.read_pod(out->compatibleHandleTypes)) { return false; }
    return true;
}

inline void serialize_VkExternalBufferProperties(::lsw::protocol::Buffer& buf, const VkExternalBufferProperties& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkExternalMemoryProperties(buf, v.externalMemoryProperties);
}

[[nodiscard]] inline bool deserialize_VkExternalBufferProperties(::lsw::protocol::BufferReader& r, VkExternalBufferProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkExternalMemoryProperties(r, &out->externalMemoryProperties, arena)) { return false; }
    return true;
}

inline void serialize_VkExternalFenceProperties(::lsw::protocol::Buffer& buf, const VkExternalFenceProperties& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.exportFromImportedHandleTypes);
    buf.write_pod(v.compatibleHandleTypes);
    buf.write_pod(v.externalFenceFeatures);
}

[[nodiscard]] inline bool deserialize_VkExternalFenceProperties(::lsw::protocol::BufferReader& r, VkExternalFenceProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->exportFromImportedHandleTypes)) { return false; }
    if (!r.read_pod(out->compatibleHandleTypes)) { return false; }
    if (!r.read_pod(out->externalFenceFeatures)) { return false; }
    return true;
}

inline void serialize_VkExternalSemaphoreProperties(::lsw::protocol::Buffer& buf, const VkExternalSemaphoreProperties& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.exportFromImportedHandleTypes);
    buf.write_pod(v.compatibleHandleTypes);
    buf.write_pod(v.externalSemaphoreFeatures);
}

[[nodiscard]] inline bool deserialize_VkExternalSemaphoreProperties(::lsw::protocol::BufferReader& r, VkExternalSemaphoreProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->exportFromImportedHandleTypes)) { return false; }
    if (!r.read_pod(out->compatibleHandleTypes)) { return false; }
    if (!r.read_pod(out->externalSemaphoreFeatures)) { return false; }
    return true;
}

inline void serialize_VkFenceCreateInfo(::lsw::protocol::Buffer& buf, const VkFenceCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkFenceCreateInfo(::lsw::protocol::BufferReader& r, VkFenceCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkFormatProperties(::lsw::protocol::Buffer& buf, const VkFormatProperties& v) {
    buf.write_pod(v.linearTilingFeatures);
    buf.write_pod(v.optimalTilingFeatures);
    buf.write_pod(v.bufferFeatures);
}

[[nodiscard]] inline bool deserialize_VkFormatProperties(::lsw::protocol::BufferReader& r, VkFormatProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->linearTilingFeatures)) { return false; }
    if (!r.read_pod(out->optimalTilingFeatures)) { return false; }
    if (!r.read_pod(out->bufferFeatures)) { return false; }
    return true;
}

inline void serialize_VkFormatProperties2(::lsw::protocol::Buffer& buf, const VkFormatProperties2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkFormatProperties(buf, v.formatProperties);
}

[[nodiscard]] inline bool deserialize_VkFormatProperties2(::lsw::protocol::BufferReader& r, VkFormatProperties2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkFormatProperties(r, &out->formatProperties, arena)) { return false; }
    return true;
}

inline void serialize_VkFramebufferCreateInfo(::lsw::protocol::Buffer& buf, const VkFramebufferCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    serialize_VkRenderPass(buf, v.renderPass);
    buf.write_pod(v.attachmentCount);
    {
        const std::uint8_t present = v.pAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.attachmentCount); ++i) {
                serialize_VkImageView(buf, v.pAttachments[i]);
            }
        }
    }
    buf.write_pod(v.width);
    buf.write_pod(v.height);
    buf.write_pod(v.layers);
}

[[nodiscard]] inline bool deserialize_VkFramebufferCreateInfo(::lsw::protocol::BufferReader& r, VkFramebufferCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!deserialize_VkRenderPass(r, out->renderPass)) { return false; }
    if (!r.read_pod(out->attachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->attachmentCount == 0) {
            out->pAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkImageView>(static_cast<std::size_t>(out->attachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->attachmentCount); ++i) {
                if (!deserialize_VkImageView(r, arr[i])) { return false; }
            }
            out->pAttachments = arr;
        }
    }
    if (!r.read_pod(out->width)) { return false; }
    if (!r.read_pod(out->height)) { return false; }
    if (!r.read_pod(out->layers)) { return false; }
    return true;
}

inline void serialize_VkGraphicsPipelineCreateInfo(::lsw::protocol::Buffer& buf, const VkGraphicsPipelineCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.stageCount);
    {
        const std::uint8_t present = v.pStages != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.stageCount); ++i) {
                serialize_VkPipelineShaderStageCreateInfo(buf, v.pStages[i]);
            }
        }
    }
    {
        const auto* p = v.pVertexInputState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineVertexInputStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pInputAssemblyState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineInputAssemblyStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pTessellationState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineTessellationStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pViewportState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineViewportStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pRasterizationState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineRasterizationStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pMultisampleState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineMultisampleStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pDepthStencilState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineDepthStencilStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pColorBlendState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineColorBlendStateCreateInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pDynamicState;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkPipelineDynamicStateCreateInfo(buf, *p);
        }
    }
    serialize_VkPipelineLayout(buf, v.layout);
    serialize_VkRenderPass(buf, v.renderPass);
    buf.write_pod(v.subpass);
    serialize_VkPipeline(buf, v.basePipelineHandle);
    buf.write_pod(v.basePipelineIndex);
}

[[nodiscard]] inline bool deserialize_VkGraphicsPipelineCreateInfo(::lsw::protocol::BufferReader& r, VkGraphicsPipelineCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->stageCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->stageCount == 0) {
            out->pStages = nullptr;
        } else {
            auto* arr = arena.alloc<VkPipelineShaderStageCreateInfo>(static_cast<std::size_t>(out->stageCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->stageCount); ++i) {
                if (!deserialize_VkPipelineShaderStageCreateInfo(r, &arr[i], arena)) { return false; }
            }
            out->pStages = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pVertexInputState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineVertexInputStateCreateInfo>();
            if (!deserialize_VkPipelineVertexInputStateCreateInfo(r, p, arena)) { return false; }
            out->pVertexInputState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pInputAssemblyState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineInputAssemblyStateCreateInfo>();
            if (!deserialize_VkPipelineInputAssemblyStateCreateInfo(r, p, arena)) { return false; }
            out->pInputAssemblyState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pTessellationState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineTessellationStateCreateInfo>();
            if (!deserialize_VkPipelineTessellationStateCreateInfo(r, p, arena)) { return false; }
            out->pTessellationState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pViewportState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineViewportStateCreateInfo>();
            if (!deserialize_VkPipelineViewportStateCreateInfo(r, p, arena)) { return false; }
            out->pViewportState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pRasterizationState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineRasterizationStateCreateInfo>();
            if (!deserialize_VkPipelineRasterizationStateCreateInfo(r, p, arena)) { return false; }
            out->pRasterizationState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pMultisampleState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineMultisampleStateCreateInfo>();
            if (!deserialize_VkPipelineMultisampleStateCreateInfo(r, p, arena)) { return false; }
            out->pMultisampleState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pDepthStencilState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineDepthStencilStateCreateInfo>();
            if (!deserialize_VkPipelineDepthStencilStateCreateInfo(r, p, arena)) { return false; }
            out->pDepthStencilState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pColorBlendState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineColorBlendStateCreateInfo>();
            if (!deserialize_VkPipelineColorBlendStateCreateInfo(r, p, arena)) { return false; }
            out->pColorBlendState = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pDynamicState = nullptr;
        } else {
            auto* p = arena.alloc<VkPipelineDynamicStateCreateInfo>();
            if (!deserialize_VkPipelineDynamicStateCreateInfo(r, p, arena)) { return false; }
            out->pDynamicState = p;
        }
    }
    if (!deserialize_VkPipelineLayout(r, out->layout)) { return false; }
    if (!deserialize_VkRenderPass(r, out->renderPass)) { return false; }
    if (!r.read_pod(out->subpass)) { return false; }
    if (!deserialize_VkPipeline(r, out->basePipelineHandle)) { return false; }
    if (!r.read_pod(out->basePipelineIndex)) { return false; }
    return true;
}

inline void serialize_VkImageBlit(::lsw::protocol::Buffer& buf, const VkImageBlit& v) {
    serialize_VkImageSubresourceLayers(buf, v.srcSubresource);
    buf.write(&v.srcOffsets[0], sizeof(v.srcOffsets));
    serialize_VkImageSubresourceLayers(buf, v.dstSubresource);
    buf.write(&v.dstOffsets[0], sizeof(v.dstOffsets));
}

[[nodiscard]] inline bool deserialize_VkImageBlit(::lsw::protocol::BufferReader& r, VkImageBlit* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkImageSubresourceLayers(r, &out->srcSubresource, arena)) { return false; }
    if (!r.read(&out->srcOffsets[0], sizeof(out->srcOffsets))) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->dstSubresource, arena)) { return false; }
    if (!r.read(&out->dstOffsets[0], sizeof(out->dstOffsets))) { return false; }
    return true;
}

inline void serialize_VkImageBlit2(::lsw::protocol::Buffer& buf, const VkImageBlit2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImageSubresourceLayers(buf, v.srcSubresource);
    buf.write(&v.srcOffsets[0], sizeof(v.srcOffsets));
    serialize_VkImageSubresourceLayers(buf, v.dstSubresource);
    buf.write(&v.dstOffsets[0], sizeof(v.dstOffsets));
}

[[nodiscard]] inline bool deserialize_VkImageBlit2(::lsw::protocol::BufferReader& r, VkImageBlit2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImageSubresourceLayers(r, &out->srcSubresource, arena)) { return false; }
    if (!r.read(&out->srcOffsets[0], sizeof(out->srcOffsets))) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->dstSubresource, arena)) { return false; }
    if (!r.read(&out->dstOffsets[0], sizeof(out->dstOffsets))) { return false; }
    return true;
}

inline void serialize_VkImageCopy(::lsw::protocol::Buffer& buf, const VkImageCopy& v) {
    serialize_VkImageSubresourceLayers(buf, v.srcSubresource);
    serialize_VkOffset3D(buf, v.srcOffset);
    serialize_VkImageSubresourceLayers(buf, v.dstSubresource);
    serialize_VkOffset3D(buf, v.dstOffset);
    serialize_VkExtent3D(buf, v.extent);
}

[[nodiscard]] inline bool deserialize_VkImageCopy(::lsw::protocol::BufferReader& r, VkImageCopy* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkImageSubresourceLayers(r, &out->srcSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->srcOffset, arena)) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->dstSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->dstOffset, arena)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->extent, arena)) { return false; }
    return true;
}

inline void serialize_VkImageCopy2(::lsw::protocol::Buffer& buf, const VkImageCopy2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImageSubresourceLayers(buf, v.srcSubresource);
    serialize_VkOffset3D(buf, v.srcOffset);
    serialize_VkImageSubresourceLayers(buf, v.dstSubresource);
    serialize_VkOffset3D(buf, v.dstOffset);
    serialize_VkExtent3D(buf, v.extent);
}

[[nodiscard]] inline bool deserialize_VkImageCopy2(::lsw::protocol::BufferReader& r, VkImageCopy2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImageSubresourceLayers(r, &out->srcSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->srcOffset, arena)) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->dstSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->dstOffset, arena)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->extent, arena)) { return false; }
    return true;
}

inline void serialize_VkImageCreateInfo(::lsw::protocol::Buffer& buf, const VkImageCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.imageType);
    buf.write_pod(v.format);
    serialize_VkExtent3D(buf, v.extent);
    buf.write_pod(v.mipLevels);
    buf.write_pod(v.arrayLayers);
    buf.write_pod(v.samples);
    buf.write_pod(v.tiling);
    buf.write_pod(v.usage);
    buf.write_pod(v.sharingMode);
    buf.write_pod(v.queueFamilyIndexCount);
    {
        const std::uint8_t present = v.pQueueFamilyIndices != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.queueFamilyIndexCount); ++i) {
                buf.write_pod(v.pQueueFamilyIndices[i]);
            }
        }
    }
    buf.write_pod(v.initialLayout);
}

[[nodiscard]] inline bool deserialize_VkImageCreateInfo(::lsw::protocol::BufferReader& r, VkImageCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->imageType)) { return false; }
    if (!r.read_pod(out->format)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->extent, arena)) { return false; }
    if (!r.read_pod(out->mipLevels)) { return false; }
    if (!r.read_pod(out->arrayLayers)) { return false; }
    if (!r.read_pod(out->samples)) { return false; }
    if (!r.read_pod(out->tiling)) { return false; }
    if (!r.read_pod(out->usage)) { return false; }
    if (!r.read_pod(out->sharingMode)) { return false; }
    if (!r.read_pod(out->queueFamilyIndexCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->queueFamilyIndexCount == 0) {
            out->pQueueFamilyIndices = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->queueFamilyIndexCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->queueFamilyIndexCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pQueueFamilyIndices = arr;
        }
    }
    if (!r.read_pod(out->initialLayout)) { return false; }
    return true;
}

inline void serialize_VkImageFormatProperties(::lsw::protocol::Buffer& buf, const VkImageFormatProperties& v) {
    serialize_VkExtent3D(buf, v.maxExtent);
    buf.write_pod(v.maxMipLevels);
    buf.write_pod(v.maxArrayLayers);
    buf.write_pod(v.sampleCounts);
    buf.write_pod(v.maxResourceSize);
}

[[nodiscard]] inline bool deserialize_VkImageFormatProperties(::lsw::protocol::BufferReader& r, VkImageFormatProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkExtent3D(r, &out->maxExtent, arena)) { return false; }
    if (!r.read_pod(out->maxMipLevels)) { return false; }
    if (!r.read_pod(out->maxArrayLayers)) { return false; }
    if (!r.read_pod(out->sampleCounts)) { return false; }
    if (!r.read_pod(out->maxResourceSize)) { return false; }
    return true;
}

inline void serialize_VkImageFormatProperties2(::lsw::protocol::Buffer& buf, const VkImageFormatProperties2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImageFormatProperties(buf, v.imageFormatProperties);
}

[[nodiscard]] inline bool deserialize_VkImageFormatProperties2(::lsw::protocol::BufferReader& r, VkImageFormatProperties2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImageFormatProperties(r, &out->imageFormatProperties, arena)) { return false; }
    return true;
}

inline void serialize_VkImageSubresourceRange(::lsw::protocol::Buffer& buf, const VkImageSubresourceRange& v) {
    buf.write_pod(v.aspectMask);
    buf.write_pod(v.baseMipLevel);
    buf.write_pod(v.levelCount);
    buf.write_pod(v.baseArrayLayer);
    buf.write_pod(v.layerCount);
}

[[nodiscard]] inline bool deserialize_VkImageSubresourceRange(::lsw::protocol::BufferReader& r, VkImageSubresourceRange* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->aspectMask)) { return false; }
    if (!r.read_pod(out->baseMipLevel)) { return false; }
    if (!r.read_pod(out->levelCount)) { return false; }
    if (!r.read_pod(out->baseArrayLayer)) { return false; }
    if (!r.read_pod(out->layerCount)) { return false; }
    return true;
}

inline void serialize_VkImageMemoryBarrier(::lsw::protocol::Buffer& buf, const VkImageMemoryBarrier& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstAccessMask);
    buf.write_pod(v.oldLayout);
    buf.write_pod(v.newLayout);
    buf.write_pod(v.srcQueueFamilyIndex);
    buf.write_pod(v.dstQueueFamilyIndex);
    serialize_VkImage(buf, v.image);
    serialize_VkImageSubresourceRange(buf, v.subresourceRange);
}

[[nodiscard]] inline bool deserialize_VkImageMemoryBarrier(::lsw::protocol::BufferReader& r, VkImageMemoryBarrier* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    if (!r.read_pod(out->oldLayout)) { return false; }
    if (!r.read_pod(out->newLayout)) { return false; }
    if (!r.read_pod(out->srcQueueFamilyIndex)) { return false; }
    if (!r.read_pod(out->dstQueueFamilyIndex)) { return false; }
    if (!deserialize_VkImage(r, out->image)) { return false; }
    if (!deserialize_VkImageSubresourceRange(r, &out->subresourceRange, arena)) { return false; }
    return true;
}

inline void serialize_VkImageMemoryBarrier2(::lsw::protocol::Buffer& buf, const VkImageMemoryBarrier2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcStageMask);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstStageMask);
    buf.write_pod(v.dstAccessMask);
    buf.write_pod(v.oldLayout);
    buf.write_pod(v.newLayout);
    buf.write_pod(v.srcQueueFamilyIndex);
    buf.write_pod(v.dstQueueFamilyIndex);
    serialize_VkImage(buf, v.image);
    serialize_VkImageSubresourceRange(buf, v.subresourceRange);
}

[[nodiscard]] inline bool deserialize_VkImageMemoryBarrier2(::lsw::protocol::BufferReader& r, VkImageMemoryBarrier2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcStageMask)) { return false; }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstStageMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    if (!r.read_pod(out->oldLayout)) { return false; }
    if (!r.read_pod(out->newLayout)) { return false; }
    if (!r.read_pod(out->srcQueueFamilyIndex)) { return false; }
    if (!r.read_pod(out->dstQueueFamilyIndex)) { return false; }
    if (!deserialize_VkImage(r, out->image)) { return false; }
    if (!deserialize_VkImageSubresourceRange(r, &out->subresourceRange, arena)) { return false; }
    return true;
}

inline void serialize_VkImageMemoryRequirementsInfo2(::lsw::protocol::Buffer& buf, const VkImageMemoryRequirementsInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImage(buf, v.image);
}

[[nodiscard]] inline bool deserialize_VkImageMemoryRequirementsInfo2(::lsw::protocol::BufferReader& r, VkImageMemoryRequirementsInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImage(r, out->image)) { return false; }
    return true;
}

inline void serialize_VkImageResolve(::lsw::protocol::Buffer& buf, const VkImageResolve& v) {
    serialize_VkImageSubresourceLayers(buf, v.srcSubresource);
    serialize_VkOffset3D(buf, v.srcOffset);
    serialize_VkImageSubresourceLayers(buf, v.dstSubresource);
    serialize_VkOffset3D(buf, v.dstOffset);
    serialize_VkExtent3D(buf, v.extent);
}

[[nodiscard]] inline bool deserialize_VkImageResolve(::lsw::protocol::BufferReader& r, VkImageResolve* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkImageSubresourceLayers(r, &out->srcSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->srcOffset, arena)) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->dstSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->dstOffset, arena)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->extent, arena)) { return false; }
    return true;
}

inline void serialize_VkImageResolve2(::lsw::protocol::Buffer& buf, const VkImageResolve2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImageSubresourceLayers(buf, v.srcSubresource);
    serialize_VkOffset3D(buf, v.srcOffset);
    serialize_VkImageSubresourceLayers(buf, v.dstSubresource);
    serialize_VkOffset3D(buf, v.dstOffset);
    serialize_VkExtent3D(buf, v.extent);
}

[[nodiscard]] inline bool deserialize_VkImageResolve2(::lsw::protocol::BufferReader& r, VkImageResolve2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImageSubresourceLayers(r, &out->srcSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->srcOffset, arena)) { return false; }
    if (!deserialize_VkImageSubresourceLayers(r, &out->dstSubresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->dstOffset, arena)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->extent, arena)) { return false; }
    return true;
}

inline void serialize_VkImageSparseMemoryRequirementsInfo2(::lsw::protocol::Buffer& buf, const VkImageSparseMemoryRequirementsInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImage(buf, v.image);
}

[[nodiscard]] inline bool deserialize_VkImageSparseMemoryRequirementsInfo2(::lsw::protocol::BufferReader& r, VkImageSparseMemoryRequirementsInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImage(r, out->image)) { return false; }
    return true;
}

inline void serialize_VkImageSubresource(::lsw::protocol::Buffer& buf, const VkImageSubresource& v) {
    buf.write_pod(v.aspectMask);
    buf.write_pod(v.mipLevel);
    buf.write_pod(v.arrayLayer);
}

[[nodiscard]] inline bool deserialize_VkImageSubresource(::lsw::protocol::BufferReader& r, VkImageSubresource* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->aspectMask)) { return false; }
    if (!r.read_pod(out->mipLevel)) { return false; }
    if (!r.read_pod(out->arrayLayer)) { return false; }
    return true;
}

inline void serialize_VkImageViewCreateInfo(::lsw::protocol::Buffer& buf, const VkImageViewCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    serialize_VkImage(buf, v.image);
    buf.write_pod(v.viewType);
    buf.write_pod(v.format);
    serialize_VkComponentMapping(buf, v.components);
    serialize_VkImageSubresourceRange(buf, v.subresourceRange);
}

[[nodiscard]] inline bool deserialize_VkImageViewCreateInfo(::lsw::protocol::BufferReader& r, VkImageViewCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!deserialize_VkImage(r, out->image)) { return false; }
    if (!r.read_pod(out->viewType)) { return false; }
    if (!r.read_pod(out->format)) { return false; }
    if (!deserialize_VkComponentMapping(r, &out->components, arena)) { return false; }
    if (!deserialize_VkImageSubresourceRange(r, &out->subresourceRange, arena)) { return false; }
    return true;
}

inline void serialize_VkInstanceCreateInfo(::lsw::protocol::Buffer& buf, const VkInstanceCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    {
        const auto* p = v.pApplicationInfo;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkApplicationInfo(buf, *p);
        }
    }
    buf.write_pod(v.enabledLayerCount);
    {
        const std::uint8_t present = v.ppEnabledLayerNames != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.enabledLayerCount); ++i) {
                const char* s = v.ppEnabledLayerNames[i];
                if (s == nullptr) {
                    buf.write_varint(0);
                } else {
                    const std::size_t n = std::strlen(s) + 1u;
                    buf.write_varint(n);
                    buf.write(s, n);
                }
            }
        }
    }
    buf.write_pod(v.enabledExtensionCount);
    {
        const std::uint8_t present = v.ppEnabledExtensionNames != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.enabledExtensionCount); ++i) {
                const char* s = v.ppEnabledExtensionNames[i];
                if (s == nullptr) {
                    buf.write_varint(0);
                } else {
                    const std::size_t n = std::strlen(s) + 1u;
                    buf.write_varint(n);
                    buf.write(s, n);
                }
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkInstanceCreateInfo(::lsw::protocol::BufferReader& r, VkInstanceCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pApplicationInfo = nullptr;
        } else {
            auto* p = arena.alloc<VkApplicationInfo>();
            if (!deserialize_VkApplicationInfo(r, p, arena)) { return false; }
            out->pApplicationInfo = p;
        }
    }
    if (!r.read_pod(out->enabledLayerCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->enabledLayerCount == 0) {
            out->ppEnabledLayerNames = nullptr;
        } else {
            auto** arr = arena.alloc<const char*>(static_cast<std::size_t>(out->enabledLayerCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->enabledLayerCount); ++i) {
                std::uint64_t n = 0;
                if (!r.read_varint(n)) { return false; }
                if (n == 0) {
                    arr[i] = nullptr;
                } else {
                    char* s = arena.alloc_chars(static_cast<std::size_t>(n));
                    if (!r.read(s, static_cast<std::size_t>(n))) { return false; }
                    arr[i] = s;
                }
            }
            out->ppEnabledLayerNames = arr;
        }
    }
    if (!r.read_pod(out->enabledExtensionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->enabledExtensionCount == 0) {
            out->ppEnabledExtensionNames = nullptr;
        } else {
            auto** arr = arena.alloc<const char*>(static_cast<std::size_t>(out->enabledExtensionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->enabledExtensionCount); ++i) {
                std::uint64_t n = 0;
                if (!r.read_varint(n)) { return false; }
                if (n == 0) {
                    arr[i] = nullptr;
                } else {
                    char* s = arena.alloc_chars(static_cast<std::size_t>(n));
                    if (!r.read(s, static_cast<std::size_t>(n))) { return false; }
                    arr[i] = s;
                }
            }
            out->ppEnabledExtensionNames = arr;
        }
    }
    return true;
}

inline void serialize_VkLayerProperties(::lsw::protocol::Buffer& buf, const VkLayerProperties& v) {
    buf.write(&v.layerName[0], sizeof(v.layerName));
    buf.write_pod(v.specVersion);
    buf.write_pod(v.implementationVersion);
    buf.write(&v.description[0], sizeof(v.description));
}

[[nodiscard]] inline bool deserialize_VkLayerProperties(::lsw::protocol::BufferReader& r, VkLayerProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read(&out->layerName[0], sizeof(out->layerName))) { return false; }
    if (!r.read_pod(out->specVersion)) { return false; }
    if (!r.read_pod(out->implementationVersion)) { return false; }
    if (!r.read(&out->description[0], sizeof(out->description))) { return false; }
    return true;
}

inline void serialize_VkMappedMemoryRange(::lsw::protocol::Buffer& buf, const VkMappedMemoryRange& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkDeviceMemory(buf, v.memory);
    buf.write_pod(v.offset);
    buf.write_pod(v.size);
}

[[nodiscard]] inline bool deserialize_VkMappedMemoryRange(::lsw::protocol::BufferReader& r, VkMappedMemoryRange* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkDeviceMemory(r, out->memory)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    return true;
}

inline void serialize_VkMemoryAllocateInfo(::lsw::protocol::Buffer& buf, const VkMemoryAllocateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.allocationSize);
    buf.write_pod(v.memoryTypeIndex);
}

[[nodiscard]] inline bool deserialize_VkMemoryAllocateInfo(::lsw::protocol::BufferReader& r, VkMemoryAllocateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->allocationSize)) { return false; }
    if (!r.read_pod(out->memoryTypeIndex)) { return false; }
    return true;
}

inline void serialize_VkMemoryBarrier(::lsw::protocol::Buffer& buf, const VkMemoryBarrier& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstAccessMask);
}

[[nodiscard]] inline bool deserialize_VkMemoryBarrier(::lsw::protocol::BufferReader& r, VkMemoryBarrier* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    return true;
}

inline void serialize_VkMemoryBarrier2(::lsw::protocol::Buffer& buf, const VkMemoryBarrier2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcStageMask);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstStageMask);
    buf.write_pod(v.dstAccessMask);
}

[[nodiscard]] inline bool deserialize_VkMemoryBarrier2(::lsw::protocol::BufferReader& r, VkMemoryBarrier2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcStageMask)) { return false; }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstStageMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    return true;
}

inline void serialize_VkMemoryHeap(::lsw::protocol::Buffer& buf, const VkMemoryHeap& v) {
    buf.write_pod(v.size);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkMemoryHeap(::lsw::protocol::BufferReader& r, VkMemoryHeap* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->size)) { return false; }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkMemoryRequirements(::lsw::protocol::Buffer& buf, const VkMemoryRequirements& v) {
    buf.write_pod(v.size);
    buf.write_pod(v.alignment);
    buf.write_pod(v.memoryTypeBits);
}

[[nodiscard]] inline bool deserialize_VkMemoryRequirements(::lsw::protocol::BufferReader& r, VkMemoryRequirements* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->size)) { return false; }
    if (!r.read_pod(out->alignment)) { return false; }
    if (!r.read_pod(out->memoryTypeBits)) { return false; }
    return true;
}

inline void serialize_VkMemoryRequirements2(::lsw::protocol::Buffer& buf, const VkMemoryRequirements2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkMemoryRequirements(buf, v.memoryRequirements);
}

[[nodiscard]] inline bool deserialize_VkMemoryRequirements2(::lsw::protocol::BufferReader& r, VkMemoryRequirements2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkMemoryRequirements(r, &out->memoryRequirements, arena)) { return false; }
    return true;
}

inline void serialize_VkMemoryType(::lsw::protocol::Buffer& buf, const VkMemoryType& v) {
    buf.write_pod(v.propertyFlags);
    buf.write_pod(v.heapIndex);
}

[[nodiscard]] inline bool deserialize_VkMemoryType(::lsw::protocol::BufferReader& r, VkMemoryType* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->propertyFlags)) { return false; }
    if (!r.read_pod(out->heapIndex)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceExternalBufferInfo(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceExternalBufferInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.usage);
    buf.write_pod(v.handleType);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceExternalBufferInfo(::lsw::protocol::BufferReader& r, VkPhysicalDeviceExternalBufferInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->usage)) { return false; }
    if (!r.read_pod(out->handleType)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceExternalFenceInfo(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceExternalFenceInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.handleType);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceExternalFenceInfo(::lsw::protocol::BufferReader& r, VkPhysicalDeviceExternalFenceInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->handleType)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceExternalSemaphoreInfo(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceExternalSemaphoreInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.handleType);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceExternalSemaphoreInfo(::lsw::protocol::BufferReader& r, VkPhysicalDeviceExternalSemaphoreInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->handleType)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceFeatures(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceFeatures& v) {
    buf.write_pod(v.robustBufferAccess);
    buf.write_pod(v.fullDrawIndexUint32);
    buf.write_pod(v.imageCubeArray);
    buf.write_pod(v.independentBlend);
    buf.write_pod(v.geometryShader);
    buf.write_pod(v.tessellationShader);
    buf.write_pod(v.sampleRateShading);
    buf.write_pod(v.dualSrcBlend);
    buf.write_pod(v.logicOp);
    buf.write_pod(v.multiDrawIndirect);
    buf.write_pod(v.drawIndirectFirstInstance);
    buf.write_pod(v.depthClamp);
    buf.write_pod(v.depthBiasClamp);
    buf.write_pod(v.fillModeNonSolid);
    buf.write_pod(v.depthBounds);
    buf.write_pod(v.wideLines);
    buf.write_pod(v.largePoints);
    buf.write_pod(v.alphaToOne);
    buf.write_pod(v.multiViewport);
    buf.write_pod(v.samplerAnisotropy);
    buf.write_pod(v.textureCompressionETC2);
    buf.write_pod(v.textureCompressionASTC_LDR);
    buf.write_pod(v.textureCompressionBC);
    buf.write_pod(v.occlusionQueryPrecise);
    buf.write_pod(v.pipelineStatisticsQuery);
    buf.write_pod(v.vertexPipelineStoresAndAtomics);
    buf.write_pod(v.fragmentStoresAndAtomics);
    buf.write_pod(v.shaderTessellationAndGeometryPointSize);
    buf.write_pod(v.shaderImageGatherExtended);
    buf.write_pod(v.shaderStorageImageExtendedFormats);
    buf.write_pod(v.shaderStorageImageMultisample);
    buf.write_pod(v.shaderStorageImageReadWithoutFormat);
    buf.write_pod(v.shaderStorageImageWriteWithoutFormat);
    buf.write_pod(v.shaderUniformBufferArrayDynamicIndexing);
    buf.write_pod(v.shaderSampledImageArrayDynamicIndexing);
    buf.write_pod(v.shaderStorageBufferArrayDynamicIndexing);
    buf.write_pod(v.shaderStorageImageArrayDynamicIndexing);
    buf.write_pod(v.shaderClipDistance);
    buf.write_pod(v.shaderCullDistance);
    buf.write_pod(v.shaderFloat64);
    buf.write_pod(v.shaderInt64);
    buf.write_pod(v.shaderInt16);
    buf.write_pod(v.shaderResourceResidency);
    buf.write_pod(v.shaderResourceMinLod);
    buf.write_pod(v.sparseBinding);
    buf.write_pod(v.sparseResidencyBuffer);
    buf.write_pod(v.sparseResidencyImage2D);
    buf.write_pod(v.sparseResidencyImage3D);
    buf.write_pod(v.sparseResidency2Samples);
    buf.write_pod(v.sparseResidency4Samples);
    buf.write_pod(v.sparseResidency8Samples);
    buf.write_pod(v.sparseResidency16Samples);
    buf.write_pod(v.sparseResidencyAliased);
    buf.write_pod(v.variableMultisampleRate);
    buf.write_pod(v.inheritedQueries);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceFeatures(::lsw::protocol::BufferReader& r, VkPhysicalDeviceFeatures* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->robustBufferAccess)) { return false; }
    if (!r.read_pod(out->fullDrawIndexUint32)) { return false; }
    if (!r.read_pod(out->imageCubeArray)) { return false; }
    if (!r.read_pod(out->independentBlend)) { return false; }
    if (!r.read_pod(out->geometryShader)) { return false; }
    if (!r.read_pod(out->tessellationShader)) { return false; }
    if (!r.read_pod(out->sampleRateShading)) { return false; }
    if (!r.read_pod(out->dualSrcBlend)) { return false; }
    if (!r.read_pod(out->logicOp)) { return false; }
    if (!r.read_pod(out->multiDrawIndirect)) { return false; }
    if (!r.read_pod(out->drawIndirectFirstInstance)) { return false; }
    if (!r.read_pod(out->depthClamp)) { return false; }
    if (!r.read_pod(out->depthBiasClamp)) { return false; }
    if (!r.read_pod(out->fillModeNonSolid)) { return false; }
    if (!r.read_pod(out->depthBounds)) { return false; }
    if (!r.read_pod(out->wideLines)) { return false; }
    if (!r.read_pod(out->largePoints)) { return false; }
    if (!r.read_pod(out->alphaToOne)) { return false; }
    if (!r.read_pod(out->multiViewport)) { return false; }
    if (!r.read_pod(out->samplerAnisotropy)) { return false; }
    if (!r.read_pod(out->textureCompressionETC2)) { return false; }
    if (!r.read_pod(out->textureCompressionASTC_LDR)) { return false; }
    if (!r.read_pod(out->textureCompressionBC)) { return false; }
    if (!r.read_pod(out->occlusionQueryPrecise)) { return false; }
    if (!r.read_pod(out->pipelineStatisticsQuery)) { return false; }
    if (!r.read_pod(out->vertexPipelineStoresAndAtomics)) { return false; }
    if (!r.read_pod(out->fragmentStoresAndAtomics)) { return false; }
    if (!r.read_pod(out->shaderTessellationAndGeometryPointSize)) { return false; }
    if (!r.read_pod(out->shaderImageGatherExtended)) { return false; }
    if (!r.read_pod(out->shaderStorageImageExtendedFormats)) { return false; }
    if (!r.read_pod(out->shaderStorageImageMultisample)) { return false; }
    if (!r.read_pod(out->shaderStorageImageReadWithoutFormat)) { return false; }
    if (!r.read_pod(out->shaderStorageImageWriteWithoutFormat)) { return false; }
    if (!r.read_pod(out->shaderUniformBufferArrayDynamicIndexing)) { return false; }
    if (!r.read_pod(out->shaderSampledImageArrayDynamicIndexing)) { return false; }
    if (!r.read_pod(out->shaderStorageBufferArrayDynamicIndexing)) { return false; }
    if (!r.read_pod(out->shaderStorageImageArrayDynamicIndexing)) { return false; }
    if (!r.read_pod(out->shaderClipDistance)) { return false; }
    if (!r.read_pod(out->shaderCullDistance)) { return false; }
    if (!r.read_pod(out->shaderFloat64)) { return false; }
    if (!r.read_pod(out->shaderInt64)) { return false; }
    if (!r.read_pod(out->shaderInt16)) { return false; }
    if (!r.read_pod(out->shaderResourceResidency)) { return false; }
    if (!r.read_pod(out->shaderResourceMinLod)) { return false; }
    if (!r.read_pod(out->sparseBinding)) { return false; }
    if (!r.read_pod(out->sparseResidencyBuffer)) { return false; }
    if (!r.read_pod(out->sparseResidencyImage2D)) { return false; }
    if (!r.read_pod(out->sparseResidencyImage3D)) { return false; }
    if (!r.read_pod(out->sparseResidency2Samples)) { return false; }
    if (!r.read_pod(out->sparseResidency4Samples)) { return false; }
    if (!r.read_pod(out->sparseResidency8Samples)) { return false; }
    if (!r.read_pod(out->sparseResidency16Samples)) { return false; }
    if (!r.read_pod(out->sparseResidencyAliased)) { return false; }
    if (!r.read_pod(out->variableMultisampleRate)) { return false; }
    if (!r.read_pod(out->inheritedQueries)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceFeatures2(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceFeatures2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkPhysicalDeviceFeatures(buf, v.features);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceFeatures2(::lsw::protocol::BufferReader& r, VkPhysicalDeviceFeatures2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkPhysicalDeviceFeatures(r, &out->features, arena)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceGroupProperties(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceGroupProperties& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.physicalDeviceCount);
    buf.write(&v.physicalDevices[0], sizeof(v.physicalDevices));
    buf.write_pod(v.subsetAllocation);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceGroupProperties(::lsw::protocol::BufferReader& r, VkPhysicalDeviceGroupProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->physicalDeviceCount)) { return false; }
    if (!r.read(&out->physicalDevices[0], sizeof(out->physicalDevices))) { return false; }
    if (!r.read_pod(out->subsetAllocation)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceImageFormatInfo2(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceImageFormatInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.format);
    buf.write_pod(v.type);
    buf.write_pod(v.tiling);
    buf.write_pod(v.usage);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceImageFormatInfo2(::lsw::protocol::BufferReader& r, VkPhysicalDeviceImageFormatInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->type)) { return false; }
    if (!r.read_pod(out->tiling)) { return false; }
    if (!r.read_pod(out->usage)) { return false; }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceLimits(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceLimits& v) {
    buf.write_pod(v.maxImageDimension1D);
    buf.write_pod(v.maxImageDimension2D);
    buf.write_pod(v.maxImageDimension3D);
    buf.write_pod(v.maxImageDimensionCube);
    buf.write_pod(v.maxImageArrayLayers);
    buf.write_pod(v.maxTexelBufferElements);
    buf.write_pod(v.maxUniformBufferRange);
    buf.write_pod(v.maxStorageBufferRange);
    buf.write_pod(v.maxPushConstantsSize);
    buf.write_pod(v.maxMemoryAllocationCount);
    buf.write_pod(v.maxSamplerAllocationCount);
    buf.write_pod(v.bufferImageGranularity);
    buf.write_pod(v.sparseAddressSpaceSize);
    buf.write_pod(v.maxBoundDescriptorSets);
    buf.write_pod(v.maxPerStageDescriptorSamplers);
    buf.write_pod(v.maxPerStageDescriptorUniformBuffers);
    buf.write_pod(v.maxPerStageDescriptorStorageBuffers);
    buf.write_pod(v.maxPerStageDescriptorSampledImages);
    buf.write_pod(v.maxPerStageDescriptorStorageImages);
    buf.write_pod(v.maxPerStageDescriptorInputAttachments);
    buf.write_pod(v.maxPerStageResources);
    buf.write_pod(v.maxDescriptorSetSamplers);
    buf.write_pod(v.maxDescriptorSetUniformBuffers);
    buf.write_pod(v.maxDescriptorSetUniformBuffersDynamic);
    buf.write_pod(v.maxDescriptorSetStorageBuffers);
    buf.write_pod(v.maxDescriptorSetStorageBuffersDynamic);
    buf.write_pod(v.maxDescriptorSetSampledImages);
    buf.write_pod(v.maxDescriptorSetStorageImages);
    buf.write_pod(v.maxDescriptorSetInputAttachments);
    buf.write_pod(v.maxVertexInputAttributes);
    buf.write_pod(v.maxVertexInputBindings);
    buf.write_pod(v.maxVertexInputAttributeOffset);
    buf.write_pod(v.maxVertexInputBindingStride);
    buf.write_pod(v.maxVertexOutputComponents);
    buf.write_pod(v.maxTessellationGenerationLevel);
    buf.write_pod(v.maxTessellationPatchSize);
    buf.write_pod(v.maxTessellationControlPerVertexInputComponents);
    buf.write_pod(v.maxTessellationControlPerVertexOutputComponents);
    buf.write_pod(v.maxTessellationControlPerPatchOutputComponents);
    buf.write_pod(v.maxTessellationControlTotalOutputComponents);
    buf.write_pod(v.maxTessellationEvaluationInputComponents);
    buf.write_pod(v.maxTessellationEvaluationOutputComponents);
    buf.write_pod(v.maxGeometryShaderInvocations);
    buf.write_pod(v.maxGeometryInputComponents);
    buf.write_pod(v.maxGeometryOutputComponents);
    buf.write_pod(v.maxGeometryOutputVertices);
    buf.write_pod(v.maxGeometryTotalOutputComponents);
    buf.write_pod(v.maxFragmentInputComponents);
    buf.write_pod(v.maxFragmentOutputAttachments);
    buf.write_pod(v.maxFragmentDualSrcAttachments);
    buf.write_pod(v.maxFragmentCombinedOutputResources);
    buf.write_pod(v.maxComputeSharedMemorySize);
    buf.write(&v.maxComputeWorkGroupCount[0], sizeof(v.maxComputeWorkGroupCount));
    buf.write_pod(v.maxComputeWorkGroupInvocations);
    buf.write(&v.maxComputeWorkGroupSize[0], sizeof(v.maxComputeWorkGroupSize));
    buf.write_pod(v.subPixelPrecisionBits);
    buf.write_pod(v.subTexelPrecisionBits);
    buf.write_pod(v.mipmapPrecisionBits);
    buf.write_pod(v.maxDrawIndexedIndexValue);
    buf.write_pod(v.maxDrawIndirectCount);
    buf.write_pod(v.maxSamplerLodBias);
    buf.write_pod(v.maxSamplerAnisotropy);
    buf.write_pod(v.maxViewports);
    buf.write(&v.maxViewportDimensions[0], sizeof(v.maxViewportDimensions));
    buf.write(&v.viewportBoundsRange[0], sizeof(v.viewportBoundsRange));
    buf.write_pod(v.viewportSubPixelBits);
    buf.write_pod(v.minMemoryMapAlignment);
    buf.write_pod(v.minTexelBufferOffsetAlignment);
    buf.write_pod(v.minUniformBufferOffsetAlignment);
    buf.write_pod(v.minStorageBufferOffsetAlignment);
    buf.write_pod(v.minTexelOffset);
    buf.write_pod(v.maxTexelOffset);
    buf.write_pod(v.minTexelGatherOffset);
    buf.write_pod(v.maxTexelGatherOffset);
    buf.write_pod(v.minInterpolationOffset);
    buf.write_pod(v.maxInterpolationOffset);
    buf.write_pod(v.subPixelInterpolationOffsetBits);
    buf.write_pod(v.maxFramebufferWidth);
    buf.write_pod(v.maxFramebufferHeight);
    buf.write_pod(v.maxFramebufferLayers);
    buf.write_pod(v.framebufferColorSampleCounts);
    buf.write_pod(v.framebufferDepthSampleCounts);
    buf.write_pod(v.framebufferStencilSampleCounts);
    buf.write_pod(v.framebufferNoAttachmentsSampleCounts);
    buf.write_pod(v.maxColorAttachments);
    buf.write_pod(v.sampledImageColorSampleCounts);
    buf.write_pod(v.sampledImageIntegerSampleCounts);
    buf.write_pod(v.sampledImageDepthSampleCounts);
    buf.write_pod(v.sampledImageStencilSampleCounts);
    buf.write_pod(v.storageImageSampleCounts);
    buf.write_pod(v.maxSampleMaskWords);
    buf.write_pod(v.timestampComputeAndGraphics);
    buf.write_pod(v.timestampPeriod);
    buf.write_pod(v.maxClipDistances);
    buf.write_pod(v.maxCullDistances);
    buf.write_pod(v.maxCombinedClipAndCullDistances);
    buf.write_pod(v.discreteQueuePriorities);
    buf.write(&v.pointSizeRange[0], sizeof(v.pointSizeRange));
    buf.write(&v.lineWidthRange[0], sizeof(v.lineWidthRange));
    buf.write_pod(v.pointSizeGranularity);
    buf.write_pod(v.lineWidthGranularity);
    buf.write_pod(v.strictLines);
    buf.write_pod(v.standardSampleLocations);
    buf.write_pod(v.optimalBufferCopyOffsetAlignment);
    buf.write_pod(v.optimalBufferCopyRowPitchAlignment);
    buf.write_pod(v.nonCoherentAtomSize);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceLimits(::lsw::protocol::BufferReader& r, VkPhysicalDeviceLimits* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->maxImageDimension1D)) { return false; }
    if (!r.read_pod(out->maxImageDimension2D)) { return false; }
    if (!r.read_pod(out->maxImageDimension3D)) { return false; }
    if (!r.read_pod(out->maxImageDimensionCube)) { return false; }
    if (!r.read_pod(out->maxImageArrayLayers)) { return false; }
    if (!r.read_pod(out->maxTexelBufferElements)) { return false; }
    if (!r.read_pod(out->maxUniformBufferRange)) { return false; }
    if (!r.read_pod(out->maxStorageBufferRange)) { return false; }
    if (!r.read_pod(out->maxPushConstantsSize)) { return false; }
    if (!r.read_pod(out->maxMemoryAllocationCount)) { return false; }
    if (!r.read_pod(out->maxSamplerAllocationCount)) { return false; }
    if (!r.read_pod(out->bufferImageGranularity)) { return false; }
    if (!r.read_pod(out->sparseAddressSpaceSize)) { return false; }
    if (!r.read_pod(out->maxBoundDescriptorSets)) { return false; }
    if (!r.read_pod(out->maxPerStageDescriptorSamplers)) { return false; }
    if (!r.read_pod(out->maxPerStageDescriptorUniformBuffers)) { return false; }
    if (!r.read_pod(out->maxPerStageDescriptorStorageBuffers)) { return false; }
    if (!r.read_pod(out->maxPerStageDescriptorSampledImages)) { return false; }
    if (!r.read_pod(out->maxPerStageDescriptorStorageImages)) { return false; }
    if (!r.read_pod(out->maxPerStageDescriptorInputAttachments)) { return false; }
    if (!r.read_pod(out->maxPerStageResources)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetSamplers)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetUniformBuffers)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetUniformBuffersDynamic)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetStorageBuffers)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetStorageBuffersDynamic)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetSampledImages)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetStorageImages)) { return false; }
    if (!r.read_pod(out->maxDescriptorSetInputAttachments)) { return false; }
    if (!r.read_pod(out->maxVertexInputAttributes)) { return false; }
    if (!r.read_pod(out->maxVertexInputBindings)) { return false; }
    if (!r.read_pod(out->maxVertexInputAttributeOffset)) { return false; }
    if (!r.read_pod(out->maxVertexInputBindingStride)) { return false; }
    if (!r.read_pod(out->maxVertexOutputComponents)) { return false; }
    if (!r.read_pod(out->maxTessellationGenerationLevel)) { return false; }
    if (!r.read_pod(out->maxTessellationPatchSize)) { return false; }
    if (!r.read_pod(out->maxTessellationControlPerVertexInputComponents)) { return false; }
    if (!r.read_pod(out->maxTessellationControlPerVertexOutputComponents)) { return false; }
    if (!r.read_pod(out->maxTessellationControlPerPatchOutputComponents)) { return false; }
    if (!r.read_pod(out->maxTessellationControlTotalOutputComponents)) { return false; }
    if (!r.read_pod(out->maxTessellationEvaluationInputComponents)) { return false; }
    if (!r.read_pod(out->maxTessellationEvaluationOutputComponents)) { return false; }
    if (!r.read_pod(out->maxGeometryShaderInvocations)) { return false; }
    if (!r.read_pod(out->maxGeometryInputComponents)) { return false; }
    if (!r.read_pod(out->maxGeometryOutputComponents)) { return false; }
    if (!r.read_pod(out->maxGeometryOutputVertices)) { return false; }
    if (!r.read_pod(out->maxGeometryTotalOutputComponents)) { return false; }
    if (!r.read_pod(out->maxFragmentInputComponents)) { return false; }
    if (!r.read_pod(out->maxFragmentOutputAttachments)) { return false; }
    if (!r.read_pod(out->maxFragmentDualSrcAttachments)) { return false; }
    if (!r.read_pod(out->maxFragmentCombinedOutputResources)) { return false; }
    if (!r.read_pod(out->maxComputeSharedMemorySize)) { return false; }
    if (!r.read(&out->maxComputeWorkGroupCount[0], sizeof(out->maxComputeWorkGroupCount))) { return false; }
    if (!r.read_pod(out->maxComputeWorkGroupInvocations)) { return false; }
    if (!r.read(&out->maxComputeWorkGroupSize[0], sizeof(out->maxComputeWorkGroupSize))) { return false; }
    if (!r.read_pod(out->subPixelPrecisionBits)) { return false; }
    if (!r.read_pod(out->subTexelPrecisionBits)) { return false; }
    if (!r.read_pod(out->mipmapPrecisionBits)) { return false; }
    if (!r.read_pod(out->maxDrawIndexedIndexValue)) { return false; }
    if (!r.read_pod(out->maxDrawIndirectCount)) { return false; }
    if (!r.read_pod(out->maxSamplerLodBias)) { return false; }
    if (!r.read_pod(out->maxSamplerAnisotropy)) { return false; }
    if (!r.read_pod(out->maxViewports)) { return false; }
    if (!r.read(&out->maxViewportDimensions[0], sizeof(out->maxViewportDimensions))) { return false; }
    if (!r.read(&out->viewportBoundsRange[0], sizeof(out->viewportBoundsRange))) { return false; }
    if (!r.read_pod(out->viewportSubPixelBits)) { return false; }
    if (!r.read_pod(out->minMemoryMapAlignment)) { return false; }
    if (!r.read_pod(out->minTexelBufferOffsetAlignment)) { return false; }
    if (!r.read_pod(out->minUniformBufferOffsetAlignment)) { return false; }
    if (!r.read_pod(out->minStorageBufferOffsetAlignment)) { return false; }
    if (!r.read_pod(out->minTexelOffset)) { return false; }
    if (!r.read_pod(out->maxTexelOffset)) { return false; }
    if (!r.read_pod(out->minTexelGatherOffset)) { return false; }
    if (!r.read_pod(out->maxTexelGatherOffset)) { return false; }
    if (!r.read_pod(out->minInterpolationOffset)) { return false; }
    if (!r.read_pod(out->maxInterpolationOffset)) { return false; }
    if (!r.read_pod(out->subPixelInterpolationOffsetBits)) { return false; }
    if (!r.read_pod(out->maxFramebufferWidth)) { return false; }
    if (!r.read_pod(out->maxFramebufferHeight)) { return false; }
    if (!r.read_pod(out->maxFramebufferLayers)) { return false; }
    if (!r.read_pod(out->framebufferColorSampleCounts)) { return false; }
    if (!r.read_pod(out->framebufferDepthSampleCounts)) { return false; }
    if (!r.read_pod(out->framebufferStencilSampleCounts)) { return false; }
    if (!r.read_pod(out->framebufferNoAttachmentsSampleCounts)) { return false; }
    if (!r.read_pod(out->maxColorAttachments)) { return false; }
    if (!r.read_pod(out->sampledImageColorSampleCounts)) { return false; }
    if (!r.read_pod(out->sampledImageIntegerSampleCounts)) { return false; }
    if (!r.read_pod(out->sampledImageDepthSampleCounts)) { return false; }
    if (!r.read_pod(out->sampledImageStencilSampleCounts)) { return false; }
    if (!r.read_pod(out->storageImageSampleCounts)) { return false; }
    if (!r.read_pod(out->maxSampleMaskWords)) { return false; }
    if (!r.read_pod(out->timestampComputeAndGraphics)) { return false; }
    if (!r.read_pod(out->timestampPeriod)) { return false; }
    if (!r.read_pod(out->maxClipDistances)) { return false; }
    if (!r.read_pod(out->maxCullDistances)) { return false; }
    if (!r.read_pod(out->maxCombinedClipAndCullDistances)) { return false; }
    if (!r.read_pod(out->discreteQueuePriorities)) { return false; }
    if (!r.read(&out->pointSizeRange[0], sizeof(out->pointSizeRange))) { return false; }
    if (!r.read(&out->lineWidthRange[0], sizeof(out->lineWidthRange))) { return false; }
    if (!r.read_pod(out->pointSizeGranularity)) { return false; }
    if (!r.read_pod(out->lineWidthGranularity)) { return false; }
    if (!r.read_pod(out->strictLines)) { return false; }
    if (!r.read_pod(out->standardSampleLocations)) { return false; }
    if (!r.read_pod(out->optimalBufferCopyOffsetAlignment)) { return false; }
    if (!r.read_pod(out->optimalBufferCopyRowPitchAlignment)) { return false; }
    if (!r.read_pod(out->nonCoherentAtomSize)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceMemoryProperties(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceMemoryProperties& v) {
    buf.write_pod(v.memoryTypeCount);
    buf.write(&v.memoryTypes[0], sizeof(v.memoryTypes));
    buf.write_pod(v.memoryHeapCount);
    buf.write(&v.memoryHeaps[0], sizeof(v.memoryHeaps));
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceMemoryProperties(::lsw::protocol::BufferReader& r, VkPhysicalDeviceMemoryProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->memoryTypeCount)) { return false; }
    if (!r.read(&out->memoryTypes[0], sizeof(out->memoryTypes))) { return false; }
    if (!r.read_pod(out->memoryHeapCount)) { return false; }
    if (!r.read(&out->memoryHeaps[0], sizeof(out->memoryHeaps))) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceMemoryProperties2(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceMemoryProperties2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkPhysicalDeviceMemoryProperties(buf, v.memoryProperties);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceMemoryProperties2(::lsw::protocol::BufferReader& r, VkPhysicalDeviceMemoryProperties2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkPhysicalDeviceMemoryProperties(r, &out->memoryProperties, arena)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceSparseProperties(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceSparseProperties& v) {
    buf.write_pod(v.residencyStandard2DBlockShape);
    buf.write_pod(v.residencyStandard2DMultisampleBlockShape);
    buf.write_pod(v.residencyStandard3DBlockShape);
    buf.write_pod(v.residencyAlignedMipSize);
    buf.write_pod(v.residencyNonResidentStrict);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceSparseProperties(::lsw::protocol::BufferReader& r, VkPhysicalDeviceSparseProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->residencyStandard2DBlockShape)) { return false; }
    if (!r.read_pod(out->residencyStandard2DMultisampleBlockShape)) { return false; }
    if (!r.read_pod(out->residencyStandard3DBlockShape)) { return false; }
    if (!r.read_pod(out->residencyAlignedMipSize)) { return false; }
    if (!r.read_pod(out->residencyNonResidentStrict)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceProperties(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceProperties& v) {
    buf.write_pod(v.apiVersion);
    buf.write_pod(v.driverVersion);
    buf.write_pod(v.vendorID);
    buf.write_pod(v.deviceID);
    buf.write_pod(v.deviceType);
    buf.write(&v.deviceName[0], sizeof(v.deviceName));
    buf.write(&v.pipelineCacheUUID[0], sizeof(v.pipelineCacheUUID));
    serialize_VkPhysicalDeviceLimits(buf, v.limits);
    serialize_VkPhysicalDeviceSparseProperties(buf, v.sparseProperties);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceProperties(::lsw::protocol::BufferReader& r, VkPhysicalDeviceProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->apiVersion)) { return false; }
    if (!r.read_pod(out->driverVersion)) { return false; }
    if (!r.read_pod(out->vendorID)) { return false; }
    if (!r.read_pod(out->deviceID)) { return false; }
    if (!r.read_pod(out->deviceType)) { return false; }
    if (!r.read(&out->deviceName[0], sizeof(out->deviceName))) { return false; }
    if (!r.read(&out->pipelineCacheUUID[0], sizeof(out->pipelineCacheUUID))) { return false; }
    if (!deserialize_VkPhysicalDeviceLimits(r, &out->limits, arena)) { return false; }
    if (!deserialize_VkPhysicalDeviceSparseProperties(r, &out->sparseProperties, arena)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceProperties2(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceProperties2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkPhysicalDeviceProperties(buf, v.properties);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceProperties2(::lsw::protocol::BufferReader& r, VkPhysicalDeviceProperties2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkPhysicalDeviceProperties(r, &out->properties, arena)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceSparseImageFormatInfo2(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceSparseImageFormatInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.format);
    buf.write_pod(v.type);
    buf.write_pod(v.samples);
    buf.write_pod(v.usage);
    buf.write_pod(v.tiling);
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceSparseImageFormatInfo2(::lsw::protocol::BufferReader& r, VkPhysicalDeviceSparseImageFormatInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->type)) { return false; }
    if (!r.read_pod(out->samples)) { return false; }
    if (!r.read_pod(out->usage)) { return false; }
    if (!r.read_pod(out->tiling)) { return false; }
    return true;
}

inline void serialize_VkPhysicalDeviceToolProperties(::lsw::protocol::Buffer& buf, const VkPhysicalDeviceToolProperties& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write(&v.name[0], sizeof(v.name));
    buf.write(&v.version[0], sizeof(v.version));
    buf.write_pod(v.purposes);
    buf.write(&v.description[0], sizeof(v.description));
    buf.write(&v.layer[0], sizeof(v.layer));
}

[[nodiscard]] inline bool deserialize_VkPhysicalDeviceToolProperties(::lsw::protocol::BufferReader& r, VkPhysicalDeviceToolProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read(&out->name[0], sizeof(out->name))) { return false; }
    if (!r.read(&out->version[0], sizeof(out->version))) { return false; }
    if (!r.read_pod(out->purposes)) { return false; }
    if (!r.read(&out->description[0], sizeof(out->description))) { return false; }
    if (!r.read(&out->layer[0], sizeof(out->layer))) { return false; }
    return true;
}

inline void serialize_VkPipelineCacheCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineCacheCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.initialDataSize);
    // TODO: unhandled member shape 'unknown_ptr' for 'pInitialData' (const void*)
    (void)v.pInitialData;
}

[[nodiscard]] inline bool deserialize_VkPipelineCacheCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineCacheCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->initialDataSize)) { return false; }
    // TODO: unhandled member shape 'unknown_ptr' for 'pInitialData' (const void*)
    (void)out->pInitialData;
    return true;
}

inline void serialize_VkPipelineColorBlendAttachmentState(::lsw::protocol::Buffer& buf, const VkPipelineColorBlendAttachmentState& v) {
    buf.write_pod(v.blendEnable);
    buf.write_pod(v.srcColorBlendFactor);
    buf.write_pod(v.dstColorBlendFactor);
    buf.write_pod(v.colorBlendOp);
    buf.write_pod(v.srcAlphaBlendFactor);
    buf.write_pod(v.dstAlphaBlendFactor);
    buf.write_pod(v.alphaBlendOp);
    buf.write_pod(v.colorWriteMask);
}

[[nodiscard]] inline bool deserialize_VkPipelineColorBlendAttachmentState(::lsw::protocol::BufferReader& r, VkPipelineColorBlendAttachmentState* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->blendEnable)) { return false; }
    if (!r.read_pod(out->srcColorBlendFactor)) { return false; }
    if (!r.read_pod(out->dstColorBlendFactor)) { return false; }
    if (!r.read_pod(out->colorBlendOp)) { return false; }
    if (!r.read_pod(out->srcAlphaBlendFactor)) { return false; }
    if (!r.read_pod(out->dstAlphaBlendFactor)) { return false; }
    if (!r.read_pod(out->alphaBlendOp)) { return false; }
    if (!r.read_pod(out->colorWriteMask)) { return false; }
    return true;
}

inline void serialize_VkPipelineColorBlendStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineColorBlendStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.logicOpEnable);
    buf.write_pod(v.logicOp);
    buf.write_pod(v.attachmentCount);
    {
        const std::uint8_t present = v.pAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.attachmentCount); ++i) {
                serialize_VkPipelineColorBlendAttachmentState(buf, v.pAttachments[i]);
            }
        }
    }
    buf.write(&v.blendConstants[0], sizeof(v.blendConstants));
}

[[nodiscard]] inline bool deserialize_VkPipelineColorBlendStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineColorBlendStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->logicOpEnable)) { return false; }
    if (!r.read_pod(out->logicOp)) { return false; }
    if (!r.read_pod(out->attachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->attachmentCount == 0) {
            out->pAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkPipelineColorBlendAttachmentState>(static_cast<std::size_t>(out->attachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->attachmentCount); ++i) {
                if (!deserialize_VkPipelineColorBlendAttachmentState(r, &arr[i], arena)) { return false; }
            }
            out->pAttachments = arr;
        }
    }
    if (!r.read(&out->blendConstants[0], sizeof(out->blendConstants))) { return false; }
    return true;
}

inline void serialize_VkStencilOpState(::lsw::protocol::Buffer& buf, const VkStencilOpState& v) {
    buf.write_pod(v.failOp);
    buf.write_pod(v.passOp);
    buf.write_pod(v.depthFailOp);
    buf.write_pod(v.compareOp);
    buf.write_pod(v.compareMask);
    buf.write_pod(v.writeMask);
    buf.write_pod(v.reference);
}

[[nodiscard]] inline bool deserialize_VkStencilOpState(::lsw::protocol::BufferReader& r, VkStencilOpState* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->failOp)) { return false; }
    if (!r.read_pod(out->passOp)) { return false; }
    if (!r.read_pod(out->depthFailOp)) { return false; }
    if (!r.read_pod(out->compareOp)) { return false; }
    if (!r.read_pod(out->compareMask)) { return false; }
    if (!r.read_pod(out->writeMask)) { return false; }
    if (!r.read_pod(out->reference)) { return false; }
    return true;
}

inline void serialize_VkPipelineDepthStencilStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineDepthStencilStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.depthTestEnable);
    buf.write_pod(v.depthWriteEnable);
    buf.write_pod(v.depthCompareOp);
    buf.write_pod(v.depthBoundsTestEnable);
    buf.write_pod(v.stencilTestEnable);
    serialize_VkStencilOpState(buf, v.front);
    serialize_VkStencilOpState(buf, v.back);
    buf.write_pod(v.minDepthBounds);
    buf.write_pod(v.maxDepthBounds);
}

[[nodiscard]] inline bool deserialize_VkPipelineDepthStencilStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineDepthStencilStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->depthTestEnable)) { return false; }
    if (!r.read_pod(out->depthWriteEnable)) { return false; }
    if (!r.read_pod(out->depthCompareOp)) { return false; }
    if (!r.read_pod(out->depthBoundsTestEnable)) { return false; }
    if (!r.read_pod(out->stencilTestEnable)) { return false; }
    if (!deserialize_VkStencilOpState(r, &out->front, arena)) { return false; }
    if (!deserialize_VkStencilOpState(r, &out->back, arena)) { return false; }
    if (!r.read_pod(out->minDepthBounds)) { return false; }
    if (!r.read_pod(out->maxDepthBounds)) { return false; }
    return true;
}

inline void serialize_VkPipelineDynamicStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineDynamicStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.dynamicStateCount);
    {
        const std::uint8_t present = v.pDynamicStates != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.dynamicStateCount); ++i) {
                buf.write_pod(v.pDynamicStates[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkPipelineDynamicStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineDynamicStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->dynamicStateCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->dynamicStateCount == 0) {
            out->pDynamicStates = nullptr;
        } else {
            auto* arr = arena.alloc<VkDynamicState>(static_cast<std::size_t>(out->dynamicStateCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->dynamicStateCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pDynamicStates = arr;
        }
    }
    return true;
}

inline void serialize_VkPipelineInputAssemblyStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineInputAssemblyStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.topology);
    buf.write_pod(v.primitiveRestartEnable);
}

[[nodiscard]] inline bool deserialize_VkPipelineInputAssemblyStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineInputAssemblyStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->topology)) { return false; }
    if (!r.read_pod(out->primitiveRestartEnable)) { return false; }
    return true;
}

inline void serialize_VkPipelineLayoutCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineLayoutCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.setLayoutCount);
    {
        const std::uint8_t present = v.pSetLayouts != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.setLayoutCount); ++i) {
                serialize_VkDescriptorSetLayout(buf, v.pSetLayouts[i]);
            }
        }
    }
    buf.write_pod(v.pushConstantRangeCount);
    {
        const std::uint8_t present = v.pPushConstantRanges != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.pushConstantRangeCount); ++i) {
                serialize_VkPushConstantRange(buf, v.pPushConstantRanges[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkPipelineLayoutCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineLayoutCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->setLayoutCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->setLayoutCount == 0) {
            out->pSetLayouts = nullptr;
        } else {
            auto* arr = arena.alloc<VkDescriptorSetLayout>(static_cast<std::size_t>(out->setLayoutCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->setLayoutCount); ++i) {
                if (!deserialize_VkDescriptorSetLayout(r, arr[i])) { return false; }
            }
            out->pSetLayouts = arr;
        }
    }
    if (!r.read_pod(out->pushConstantRangeCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->pushConstantRangeCount == 0) {
            out->pPushConstantRanges = nullptr;
        } else {
            auto* arr = arena.alloc<VkPushConstantRange>(static_cast<std::size_t>(out->pushConstantRangeCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->pushConstantRangeCount); ++i) {
                if (!deserialize_VkPushConstantRange(r, &arr[i], arena)) { return false; }
            }
            out->pPushConstantRanges = arr;
        }
    }
    return true;
}

inline void serialize_VkPipelineMultisampleStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineMultisampleStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.rasterizationSamples);
    buf.write_pod(v.sampleShadingEnable);
    buf.write_pod(v.minSampleShading);
    {
        const std::uint8_t present = v.pSampleMask != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>((v.rasterizationSamples + 31) / 32); ++i) {
                buf.write_pod(v.pSampleMask[i]);
            }
        }
    }
    buf.write_pod(v.alphaToCoverageEnable);
    buf.write_pod(v.alphaToOneEnable);
}

[[nodiscard]] inline bool deserialize_VkPipelineMultisampleStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineMultisampleStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->rasterizationSamples)) { return false; }
    if (!r.read_pod(out->sampleShadingEnable)) { return false; }
    if (!r.read_pod(out->minSampleShading)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || (out->rasterizationSamples + 31) / 32 == 0) {
            out->pSampleMask = nullptr;
        } else {
            auto* arr = arena.alloc<VkSampleMask>(static_cast<std::size_t>((out->rasterizationSamples + 31) / 32));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>((out->rasterizationSamples + 31) / 32); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pSampleMask = arr;
        }
    }
    if (!r.read_pod(out->alphaToCoverageEnable)) { return false; }
    if (!r.read_pod(out->alphaToOneEnable)) { return false; }
    return true;
}

inline void serialize_VkPipelineRasterizationStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineRasterizationStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.depthClampEnable);
    buf.write_pod(v.rasterizerDiscardEnable);
    buf.write_pod(v.polygonMode);
    buf.write_pod(v.cullMode);
    buf.write_pod(v.frontFace);
    buf.write_pod(v.depthBiasEnable);
    buf.write_pod(v.depthBiasConstantFactor);
    buf.write_pod(v.depthBiasClamp);
    buf.write_pod(v.depthBiasSlopeFactor);
    buf.write_pod(v.lineWidth);
}

[[nodiscard]] inline bool deserialize_VkPipelineRasterizationStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineRasterizationStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->depthClampEnable)) { return false; }
    if (!r.read_pod(out->rasterizerDiscardEnable)) { return false; }
    if (!r.read_pod(out->polygonMode)) { return false; }
    if (!r.read_pod(out->cullMode)) { return false; }
    if (!r.read_pod(out->frontFace)) { return false; }
    if (!r.read_pod(out->depthBiasEnable)) { return false; }
    if (!r.read_pod(out->depthBiasConstantFactor)) { return false; }
    if (!r.read_pod(out->depthBiasClamp)) { return false; }
    if (!r.read_pod(out->depthBiasSlopeFactor)) { return false; }
    if (!r.read_pod(out->lineWidth)) { return false; }
    return true;
}

inline void serialize_VkPipelineTessellationStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineTessellationStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.patchControlPoints);
}

[[nodiscard]] inline bool deserialize_VkPipelineTessellationStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineTessellationStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->patchControlPoints)) { return false; }
    return true;
}

inline void serialize_VkPipelineVertexInputStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineVertexInputStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.vertexBindingDescriptionCount);
    {
        const std::uint8_t present = v.pVertexBindingDescriptions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.vertexBindingDescriptionCount); ++i) {
                serialize_VkVertexInputBindingDescription(buf, v.pVertexBindingDescriptions[i]);
            }
        }
    }
    buf.write_pod(v.vertexAttributeDescriptionCount);
    {
        const std::uint8_t present = v.pVertexAttributeDescriptions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.vertexAttributeDescriptionCount); ++i) {
                serialize_VkVertexInputAttributeDescription(buf, v.pVertexAttributeDescriptions[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkPipelineVertexInputStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineVertexInputStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->vertexBindingDescriptionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->vertexBindingDescriptionCount == 0) {
            out->pVertexBindingDescriptions = nullptr;
        } else {
            auto* arr = arena.alloc<VkVertexInputBindingDescription>(static_cast<std::size_t>(out->vertexBindingDescriptionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->vertexBindingDescriptionCount); ++i) {
                if (!deserialize_VkVertexInputBindingDescription(r, &arr[i], arena)) { return false; }
            }
            out->pVertexBindingDescriptions = arr;
        }
    }
    if (!r.read_pod(out->vertexAttributeDescriptionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->vertexAttributeDescriptionCount == 0) {
            out->pVertexAttributeDescriptions = nullptr;
        } else {
            auto* arr = arena.alloc<VkVertexInputAttributeDescription>(static_cast<std::size_t>(out->vertexAttributeDescriptionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->vertexAttributeDescriptionCount); ++i) {
                if (!deserialize_VkVertexInputAttributeDescription(r, &arr[i], arena)) { return false; }
            }
            out->pVertexAttributeDescriptions = arr;
        }
    }
    return true;
}

inline void serialize_VkPipelineViewportStateCreateInfo(::lsw::protocol::Buffer& buf, const VkPipelineViewportStateCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.viewportCount);
    {
        const std::uint8_t present = v.pViewports != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.viewportCount); ++i) {
                serialize_VkViewport(buf, v.pViewports[i]);
            }
        }
    }
    buf.write_pod(v.scissorCount);
    {
        const std::uint8_t present = v.pScissors != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.scissorCount); ++i) {
                serialize_VkRect2D(buf, v.pScissors[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkPipelineViewportStateCreateInfo(::lsw::protocol::BufferReader& r, VkPipelineViewportStateCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->viewportCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->viewportCount == 0) {
            out->pViewports = nullptr;
        } else {
            auto* arr = arena.alloc<VkViewport>(static_cast<std::size_t>(out->viewportCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->viewportCount); ++i) {
                if (!deserialize_VkViewport(r, &arr[i], arena)) { return false; }
            }
            out->pViewports = arr;
        }
    }
    if (!r.read_pod(out->scissorCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->scissorCount == 0) {
            out->pScissors = nullptr;
        } else {
            auto* arr = arena.alloc<VkRect2D>(static_cast<std::size_t>(out->scissorCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->scissorCount); ++i) {
                if (!deserialize_VkRect2D(r, &arr[i], arena)) { return false; }
            }
            out->pScissors = arr;
        }
    }
    return true;
}

inline void serialize_VkPresentInfoKHR(::lsw::protocol::Buffer& buf, const VkPresentInfoKHR& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.waitSemaphoreCount);
    {
        const std::uint8_t present = v.pWaitSemaphores != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.waitSemaphoreCount); ++i) {
                serialize_VkSemaphore(buf, v.pWaitSemaphores[i]);
            }
        }
    }
    buf.write_pod(v.swapchainCount);
    {
        const std::uint8_t present = v.pSwapchains != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.swapchainCount); ++i) {
                serialize_VkSwapchainKHR(buf, v.pSwapchains[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pImageIndices != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.swapchainCount); ++i) {
                buf.write_pod(v.pImageIndices[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pResults != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.swapchainCount); ++i) {
                buf.write_pod(v.pResults[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkPresentInfoKHR(::lsw::protocol::BufferReader& r, VkPresentInfoKHR* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->waitSemaphoreCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->waitSemaphoreCount == 0) {
            out->pWaitSemaphores = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphore>(static_cast<std::size_t>(out->waitSemaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->waitSemaphoreCount); ++i) {
                if (!deserialize_VkSemaphore(r, arr[i])) { return false; }
            }
            out->pWaitSemaphores = arr;
        }
    }
    if (!r.read_pod(out->swapchainCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->swapchainCount == 0) {
            out->pSwapchains = nullptr;
        } else {
            auto* arr = arena.alloc<VkSwapchainKHR>(static_cast<std::size_t>(out->swapchainCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->swapchainCount); ++i) {
                if (!deserialize_VkSwapchainKHR(r, arr[i])) { return false; }
            }
            out->pSwapchains = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->swapchainCount == 0) {
            out->pImageIndices = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->swapchainCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->swapchainCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pImageIndices = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->swapchainCount == 0) {
            out->pResults = nullptr;
        } else {
            auto* arr = arena.alloc<VkResult>(static_cast<std::size_t>(out->swapchainCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->swapchainCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pResults = arr;
        }
    }
    return true;
}

inline void serialize_VkPrivateDataSlotCreateInfo(::lsw::protocol::Buffer& buf, const VkPrivateDataSlotCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkPrivateDataSlotCreateInfo(::lsw::protocol::BufferReader& r, VkPrivateDataSlotCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkPushConstantRange(::lsw::protocol::Buffer& buf, const VkPushConstantRange& v) {
    buf.write_pod(v.stageFlags);
    buf.write_pod(v.offset);
    buf.write_pod(v.size);
}

[[nodiscard]] inline bool deserialize_VkPushConstantRange(::lsw::protocol::BufferReader& r, VkPushConstantRange* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->stageFlags)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    return true;
}

inline void serialize_VkQueryPoolCreateInfo(::lsw::protocol::Buffer& buf, const VkQueryPoolCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.queryType);
    buf.write_pod(v.queryCount);
    buf.write_pod(v.pipelineStatistics);
}

[[nodiscard]] inline bool deserialize_VkQueryPoolCreateInfo(::lsw::protocol::BufferReader& r, VkQueryPoolCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->queryType)) { return false; }
    if (!r.read_pod(out->queryCount)) { return false; }
    if (!r.read_pod(out->pipelineStatistics)) { return false; }
    return true;
}

inline void serialize_VkQueueFamilyProperties(::lsw::protocol::Buffer& buf, const VkQueueFamilyProperties& v) {
    buf.write_pod(v.queueFlags);
    buf.write_pod(v.queueCount);
    buf.write_pod(v.timestampValidBits);
    serialize_VkExtent3D(buf, v.minImageTransferGranularity);
}

[[nodiscard]] inline bool deserialize_VkQueueFamilyProperties(::lsw::protocol::BufferReader& r, VkQueueFamilyProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->queueFlags)) { return false; }
    if (!r.read_pod(out->queueCount)) { return false; }
    if (!r.read_pod(out->timestampValidBits)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->minImageTransferGranularity, arena)) { return false; }
    return true;
}

inline void serialize_VkQueueFamilyProperties2(::lsw::protocol::Buffer& buf, const VkQueueFamilyProperties2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkQueueFamilyProperties(buf, v.queueFamilyProperties);
}

[[nodiscard]] inline bool deserialize_VkQueueFamilyProperties2(::lsw::protocol::BufferReader& r, VkQueueFamilyProperties2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkQueueFamilyProperties(r, &out->queueFamilyProperties, arena)) { return false; }
    return true;
}

inline void serialize_VkRenderPassBeginInfo(::lsw::protocol::Buffer& buf, const VkRenderPassBeginInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkRenderPass(buf, v.renderPass);
    serialize_VkFramebuffer(buf, v.framebuffer);
    serialize_VkRect2D(buf, v.renderArea);
    buf.write_pod(v.clearValueCount);
    {
        const std::uint8_t present = v.pClearValues != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.clearValueCount); ++i) {
                serialize_VkClearValue(buf, v.pClearValues[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkRenderPassBeginInfo(::lsw::protocol::BufferReader& r, VkRenderPassBeginInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkRenderPass(r, out->renderPass)) { return false; }
    if (!deserialize_VkFramebuffer(r, out->framebuffer)) { return false; }
    if (!deserialize_VkRect2D(r, &out->renderArea, arena)) { return false; }
    if (!r.read_pod(out->clearValueCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->clearValueCount == 0) {
            out->pClearValues = nullptr;
        } else {
            auto* arr = arena.alloc<VkClearValue>(static_cast<std::size_t>(out->clearValueCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->clearValueCount); ++i) {
                if (!deserialize_VkClearValue(r, &arr[i], arena)) { return false; }
            }
            out->pClearValues = arr;
        }
    }
    return true;
}

inline void serialize_VkRenderPassCreateInfo(::lsw::protocol::Buffer& buf, const VkRenderPassCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.attachmentCount);
    {
        const std::uint8_t present = v.pAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.attachmentCount); ++i) {
                serialize_VkAttachmentDescription(buf, v.pAttachments[i]);
            }
        }
    }
    buf.write_pod(v.subpassCount);
    {
        const std::uint8_t present = v.pSubpasses != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.subpassCount); ++i) {
                serialize_VkSubpassDescription(buf, v.pSubpasses[i]);
            }
        }
    }
    buf.write_pod(v.dependencyCount);
    {
        const std::uint8_t present = v.pDependencies != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.dependencyCount); ++i) {
                serialize_VkSubpassDependency(buf, v.pDependencies[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkRenderPassCreateInfo(::lsw::protocol::BufferReader& r, VkRenderPassCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->attachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->attachmentCount == 0) {
            out->pAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentDescription>(static_cast<std::size_t>(out->attachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->attachmentCount); ++i) {
                if (!deserialize_VkAttachmentDescription(r, &arr[i], arena)) { return false; }
            }
            out->pAttachments = arr;
        }
    }
    if (!r.read_pod(out->subpassCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->subpassCount == 0) {
            out->pSubpasses = nullptr;
        } else {
            auto* arr = arena.alloc<VkSubpassDescription>(static_cast<std::size_t>(out->subpassCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->subpassCount); ++i) {
                if (!deserialize_VkSubpassDescription(r, &arr[i], arena)) { return false; }
            }
            out->pSubpasses = arr;
        }
    }
    if (!r.read_pod(out->dependencyCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->dependencyCount == 0) {
            out->pDependencies = nullptr;
        } else {
            auto* arr = arena.alloc<VkSubpassDependency>(static_cast<std::size_t>(out->dependencyCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->dependencyCount); ++i) {
                if (!deserialize_VkSubpassDependency(r, &arr[i], arena)) { return false; }
            }
            out->pDependencies = arr;
        }
    }
    return true;
}

inline void serialize_VkRenderPassCreateInfo2(::lsw::protocol::Buffer& buf, const VkRenderPassCreateInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.attachmentCount);
    {
        const std::uint8_t present = v.pAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.attachmentCount); ++i) {
                serialize_VkAttachmentDescription2(buf, v.pAttachments[i]);
            }
        }
    }
    buf.write_pod(v.subpassCount);
    {
        const std::uint8_t present = v.pSubpasses != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.subpassCount); ++i) {
                serialize_VkSubpassDescription2(buf, v.pSubpasses[i]);
            }
        }
    }
    buf.write_pod(v.dependencyCount);
    {
        const std::uint8_t present = v.pDependencies != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.dependencyCount); ++i) {
                serialize_VkSubpassDependency2(buf, v.pDependencies[i]);
            }
        }
    }
    buf.write_pod(v.correlatedViewMaskCount);
    {
        const std::uint8_t present = v.pCorrelatedViewMasks != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.correlatedViewMaskCount); ++i) {
                buf.write_pod(v.pCorrelatedViewMasks[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkRenderPassCreateInfo2(::lsw::protocol::BufferReader& r, VkRenderPassCreateInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->attachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->attachmentCount == 0) {
            out->pAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentDescription2>(static_cast<std::size_t>(out->attachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->attachmentCount); ++i) {
                if (!deserialize_VkAttachmentDescription2(r, &arr[i], arena)) { return false; }
            }
            out->pAttachments = arr;
        }
    }
    if (!r.read_pod(out->subpassCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->subpassCount == 0) {
            out->pSubpasses = nullptr;
        } else {
            auto* arr = arena.alloc<VkSubpassDescription2>(static_cast<std::size_t>(out->subpassCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->subpassCount); ++i) {
                if (!deserialize_VkSubpassDescription2(r, &arr[i], arena)) { return false; }
            }
            out->pSubpasses = arr;
        }
    }
    if (!r.read_pod(out->dependencyCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->dependencyCount == 0) {
            out->pDependencies = nullptr;
        } else {
            auto* arr = arena.alloc<VkSubpassDependency2>(static_cast<std::size_t>(out->dependencyCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->dependencyCount); ++i) {
                if (!deserialize_VkSubpassDependency2(r, &arr[i], arena)) { return false; }
            }
            out->pDependencies = arr;
        }
    }
    if (!r.read_pod(out->correlatedViewMaskCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->correlatedViewMaskCount == 0) {
            out->pCorrelatedViewMasks = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->correlatedViewMaskCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->correlatedViewMaskCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pCorrelatedViewMasks = arr;
        }
    }
    return true;
}

inline void serialize_VkRenderingAttachmentInfo(::lsw::protocol::Buffer& buf, const VkRenderingAttachmentInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImageView(buf, v.imageView);
    buf.write_pod(v.imageLayout);
    buf.write_pod(v.resolveMode);
    serialize_VkImageView(buf, v.resolveImageView);
    buf.write_pod(v.resolveImageLayout);
    buf.write_pod(v.loadOp);
    buf.write_pod(v.storeOp);
    serialize_VkClearValue(buf, v.clearValue);
}

[[nodiscard]] inline bool deserialize_VkRenderingAttachmentInfo(::lsw::protocol::BufferReader& r, VkRenderingAttachmentInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImageView(r, out->imageView)) { return false; }
    if (!r.read_pod(out->imageLayout)) { return false; }
    if (!r.read_pod(out->resolveMode)) { return false; }
    if (!deserialize_VkImageView(r, out->resolveImageView)) { return false; }
    if (!r.read_pod(out->resolveImageLayout)) { return false; }
    if (!r.read_pod(out->loadOp)) { return false; }
    if (!r.read_pod(out->storeOp)) { return false; }
    if (!deserialize_VkClearValue(r, &out->clearValue, arena)) { return false; }
    return true;
}

inline void serialize_VkRenderingInfo(::lsw::protocol::Buffer& buf, const VkRenderingInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    serialize_VkRect2D(buf, v.renderArea);
    buf.write_pod(v.layerCount);
    buf.write_pod(v.viewMask);
    buf.write_pod(v.colorAttachmentCount);
    {
        const std::uint8_t present = v.pColorAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.colorAttachmentCount); ++i) {
                serialize_VkRenderingAttachmentInfo(buf, v.pColorAttachments[i]);
            }
        }
    }
    {
        const auto* p = v.pDepthAttachment;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkRenderingAttachmentInfo(buf, *p);
        }
    }
    {
        const auto* p = v.pStencilAttachment;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkRenderingAttachmentInfo(buf, *p);
        }
    }
}

[[nodiscard]] inline bool deserialize_VkRenderingInfo(::lsw::protocol::BufferReader& r, VkRenderingInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!deserialize_VkRect2D(r, &out->renderArea, arena)) { return false; }
    if (!r.read_pod(out->layerCount)) { return false; }
    if (!r.read_pod(out->viewMask)) { return false; }
    if (!r.read_pod(out->colorAttachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->colorAttachmentCount == 0) {
            out->pColorAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkRenderingAttachmentInfo>(static_cast<std::size_t>(out->colorAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->colorAttachmentCount); ++i) {
                if (!deserialize_VkRenderingAttachmentInfo(r, &arr[i], arena)) { return false; }
            }
            out->pColorAttachments = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pDepthAttachment = nullptr;
        } else {
            auto* p = arena.alloc<VkRenderingAttachmentInfo>();
            if (!deserialize_VkRenderingAttachmentInfo(r, p, arena)) { return false; }
            out->pDepthAttachment = p;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pStencilAttachment = nullptr;
        } else {
            auto* p = arena.alloc<VkRenderingAttachmentInfo>();
            if (!deserialize_VkRenderingAttachmentInfo(r, p, arena)) { return false; }
            out->pStencilAttachment = p;
        }
    }
    return true;
}

inline void serialize_VkResolveImageInfo2(::lsw::protocol::Buffer& buf, const VkResolveImageInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkImage(buf, v.srcImage);
    buf.write_pod(v.srcImageLayout);
    serialize_VkImage(buf, v.dstImage);
    buf.write_pod(v.dstImageLayout);
    buf.write_pod(v.regionCount);
    {
        const std::uint8_t present = v.pRegions != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.regionCount); ++i) {
                serialize_VkImageResolve2(buf, v.pRegions[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkResolveImageInfo2(::lsw::protocol::BufferReader& r, VkResolveImageInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkImage(r, out->srcImage)) { return false; }
    if (!r.read_pod(out->srcImageLayout)) { return false; }
    if (!deserialize_VkImage(r, out->dstImage)) { return false; }
    if (!r.read_pod(out->dstImageLayout)) { return false; }
    if (!r.read_pod(out->regionCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->regionCount == 0) {
            out->pRegions = nullptr;
        } else {
            auto* arr = arena.alloc<VkImageResolve2>(static_cast<std::size_t>(out->regionCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->regionCount); ++i) {
                if (!deserialize_VkImageResolve2(r, &arr[i], arena)) { return false; }
            }
            out->pRegions = arr;
        }
    }
    return true;
}

inline void serialize_VkSamplerCreateInfo(::lsw::protocol::Buffer& buf, const VkSamplerCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.magFilter);
    buf.write_pod(v.minFilter);
    buf.write_pod(v.mipmapMode);
    buf.write_pod(v.addressModeU);
    buf.write_pod(v.addressModeV);
    buf.write_pod(v.addressModeW);
    buf.write_pod(v.mipLodBias);
    buf.write_pod(v.anisotropyEnable);
    buf.write_pod(v.maxAnisotropy);
    buf.write_pod(v.compareEnable);
    buf.write_pod(v.compareOp);
    buf.write_pod(v.minLod);
    buf.write_pod(v.maxLod);
    buf.write_pod(v.borderColor);
    buf.write_pod(v.unnormalizedCoordinates);
}

[[nodiscard]] inline bool deserialize_VkSamplerCreateInfo(::lsw::protocol::BufferReader& r, VkSamplerCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->magFilter)) { return false; }
    if (!r.read_pod(out->minFilter)) { return false; }
    if (!r.read_pod(out->mipmapMode)) { return false; }
    if (!r.read_pod(out->addressModeU)) { return false; }
    if (!r.read_pod(out->addressModeV)) { return false; }
    if (!r.read_pod(out->addressModeW)) { return false; }
    if (!r.read_pod(out->mipLodBias)) { return false; }
    if (!r.read_pod(out->anisotropyEnable)) { return false; }
    if (!r.read_pod(out->maxAnisotropy)) { return false; }
    if (!r.read_pod(out->compareEnable)) { return false; }
    if (!r.read_pod(out->compareOp)) { return false; }
    if (!r.read_pod(out->minLod)) { return false; }
    if (!r.read_pod(out->maxLod)) { return false; }
    if (!r.read_pod(out->borderColor)) { return false; }
    if (!r.read_pod(out->unnormalizedCoordinates)) { return false; }
    return true;
}

inline void serialize_VkSamplerYcbcrConversionCreateInfo(::lsw::protocol::Buffer& buf, const VkSamplerYcbcrConversionCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.format);
    buf.write_pod(v.ycbcrModel);
    buf.write_pod(v.ycbcrRange);
    serialize_VkComponentMapping(buf, v.components);
    buf.write_pod(v.xChromaOffset);
    buf.write_pod(v.yChromaOffset);
    buf.write_pod(v.chromaFilter);
    buf.write_pod(v.forceExplicitReconstruction);
}

[[nodiscard]] inline bool deserialize_VkSamplerYcbcrConversionCreateInfo(::lsw::protocol::BufferReader& r, VkSamplerYcbcrConversionCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->ycbcrModel)) { return false; }
    if (!r.read_pod(out->ycbcrRange)) { return false; }
    if (!deserialize_VkComponentMapping(r, &out->components, arena)) { return false; }
    if (!r.read_pod(out->xChromaOffset)) { return false; }
    if (!r.read_pod(out->yChromaOffset)) { return false; }
    if (!r.read_pod(out->chromaFilter)) { return false; }
    if (!r.read_pod(out->forceExplicitReconstruction)) { return false; }
    return true;
}

inline void serialize_VkSemaphoreCreateInfo(::lsw::protocol::Buffer& buf, const VkSemaphoreCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkSemaphoreCreateInfo(::lsw::protocol::BufferReader& r, VkSemaphoreCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkSemaphoreSignalInfo(::lsw::protocol::Buffer& buf, const VkSemaphoreSignalInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkSemaphore(buf, v.semaphore);
    buf.write_pod(v.value);
}

[[nodiscard]] inline bool deserialize_VkSemaphoreSignalInfo(::lsw::protocol::BufferReader& r, VkSemaphoreSignalInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkSemaphore(r, out->semaphore)) { return false; }
    if (!r.read_pod(out->value)) { return false; }
    return true;
}

inline void serialize_VkSemaphoreSubmitInfo(::lsw::protocol::Buffer& buf, const VkSemaphoreSubmitInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkSemaphore(buf, v.semaphore);
    buf.write_pod(v.value);
    buf.write_pod(v.stageMask);
    buf.write_pod(v.deviceIndex);
}

[[nodiscard]] inline bool deserialize_VkSemaphoreSubmitInfo(::lsw::protocol::BufferReader& r, VkSemaphoreSubmitInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkSemaphore(r, out->semaphore)) { return false; }
    if (!r.read_pod(out->value)) { return false; }
    if (!r.read_pod(out->stageMask)) { return false; }
    if (!r.read_pod(out->deviceIndex)) { return false; }
    return true;
}

inline void serialize_VkSemaphoreWaitInfo(::lsw::protocol::Buffer& buf, const VkSemaphoreWaitInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.semaphoreCount);
    {
        const std::uint8_t present = v.pSemaphores != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.semaphoreCount); ++i) {
                serialize_VkSemaphore(buf, v.pSemaphores[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pValues != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.semaphoreCount); ++i) {
                buf.write_pod(v.pValues[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSemaphoreWaitInfo(::lsw::protocol::BufferReader& r, VkSemaphoreWaitInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->semaphoreCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->semaphoreCount == 0) {
            out->pSemaphores = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphore>(static_cast<std::size_t>(out->semaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->semaphoreCount); ++i) {
                if (!deserialize_VkSemaphore(r, arr[i])) { return false; }
            }
            out->pSemaphores = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->semaphoreCount == 0) {
            out->pValues = nullptr;
        } else {
            auto* arr = arena.alloc<uint64_t>(static_cast<std::size_t>(out->semaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->semaphoreCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pValues = arr;
        }
    }
    return true;
}

inline void serialize_VkShaderModuleCreateInfo(::lsw::protocol::Buffer& buf, const VkShaderModuleCreateInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.codeSize);
    {
        const std::uint8_t present = v.pCode != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.codeSize / 4); ++i) {
                buf.write_pod(v.pCode[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkShaderModuleCreateInfo(::lsw::protocol::BufferReader& r, VkShaderModuleCreateInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->codeSize)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->codeSize / 4 == 0) {
            out->pCode = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->codeSize / 4));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->codeSize / 4); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pCode = arr;
        }
    }
    return true;
}

inline void serialize_VkSparseBufferMemoryBindInfo(::lsw::protocol::Buffer& buf, const VkSparseBufferMemoryBindInfo& v) {
    serialize_VkBuffer(buf, v.buffer);
    buf.write_pod(v.bindCount);
    {
        const std::uint8_t present = v.pBinds != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.bindCount); ++i) {
                serialize_VkSparseMemoryBind(buf, v.pBinds[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSparseBufferMemoryBindInfo(::lsw::protocol::BufferReader& r, VkSparseBufferMemoryBindInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkBuffer(r, out->buffer)) { return false; }
    if (!r.read_pod(out->bindCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->bindCount == 0) {
            out->pBinds = nullptr;
        } else {
            auto* arr = arena.alloc<VkSparseMemoryBind>(static_cast<std::size_t>(out->bindCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->bindCount); ++i) {
                if (!deserialize_VkSparseMemoryBind(r, &arr[i], arena)) { return false; }
            }
            out->pBinds = arr;
        }
    }
    return true;
}

inline void serialize_VkSparseImageFormatProperties(::lsw::protocol::Buffer& buf, const VkSparseImageFormatProperties& v) {
    buf.write_pod(v.aspectMask);
    serialize_VkExtent3D(buf, v.imageGranularity);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkSparseImageFormatProperties(::lsw::protocol::BufferReader& r, VkSparseImageFormatProperties* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->aspectMask)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->imageGranularity, arena)) { return false; }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkSparseImageFormatProperties2(::lsw::protocol::Buffer& buf, const VkSparseImageFormatProperties2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkSparseImageFormatProperties(buf, v.properties);
}

[[nodiscard]] inline bool deserialize_VkSparseImageFormatProperties2(::lsw::protocol::BufferReader& r, VkSparseImageFormatProperties2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkSparseImageFormatProperties(r, &out->properties, arena)) { return false; }
    return true;
}

inline void serialize_VkSparseImageMemoryBind(::lsw::protocol::Buffer& buf, const VkSparseImageMemoryBind& v) {
    serialize_VkImageSubresource(buf, v.subresource);
    serialize_VkOffset3D(buf, v.offset);
    serialize_VkExtent3D(buf, v.extent);
    serialize_VkDeviceMemory(buf, v.memory);
    buf.write_pod(v.memoryOffset);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkSparseImageMemoryBind(::lsw::protocol::BufferReader& r, VkSparseImageMemoryBind* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkImageSubresource(r, &out->subresource, arena)) { return false; }
    if (!deserialize_VkOffset3D(r, &out->offset, arena)) { return false; }
    if (!deserialize_VkExtent3D(r, &out->extent, arena)) { return false; }
    if (!deserialize_VkDeviceMemory(r, out->memory)) { return false; }
    if (!r.read_pod(out->memoryOffset)) { return false; }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkSparseImageMemoryBindInfo(::lsw::protocol::Buffer& buf, const VkSparseImageMemoryBindInfo& v) {
    serialize_VkImage(buf, v.image);
    buf.write_pod(v.bindCount);
    {
        const std::uint8_t present = v.pBinds != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.bindCount); ++i) {
                serialize_VkSparseImageMemoryBind(buf, v.pBinds[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSparseImageMemoryBindInfo(::lsw::protocol::BufferReader& r, VkSparseImageMemoryBindInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkImage(r, out->image)) { return false; }
    if (!r.read_pod(out->bindCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->bindCount == 0) {
            out->pBinds = nullptr;
        } else {
            auto* arr = arena.alloc<VkSparseImageMemoryBind>(static_cast<std::size_t>(out->bindCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->bindCount); ++i) {
                if (!deserialize_VkSparseImageMemoryBind(r, &arr[i], arena)) { return false; }
            }
            out->pBinds = arr;
        }
    }
    return true;
}

inline void serialize_VkSparseImageMemoryRequirements(::lsw::protocol::Buffer& buf, const VkSparseImageMemoryRequirements& v) {
    serialize_VkSparseImageFormatProperties(buf, v.formatProperties);
    buf.write_pod(v.imageMipTailFirstLod);
    buf.write_pod(v.imageMipTailSize);
    buf.write_pod(v.imageMipTailOffset);
    buf.write_pod(v.imageMipTailStride);
}

[[nodiscard]] inline bool deserialize_VkSparseImageMemoryRequirements(::lsw::protocol::BufferReader& r, VkSparseImageMemoryRequirements* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkSparseImageFormatProperties(r, &out->formatProperties, arena)) { return false; }
    if (!r.read_pod(out->imageMipTailFirstLod)) { return false; }
    if (!r.read_pod(out->imageMipTailSize)) { return false; }
    if (!r.read_pod(out->imageMipTailOffset)) { return false; }
    if (!r.read_pod(out->imageMipTailStride)) { return false; }
    return true;
}

inline void serialize_VkSparseImageMemoryRequirements2(::lsw::protocol::Buffer& buf, const VkSparseImageMemoryRequirements2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkSparseImageMemoryRequirements(buf, v.memoryRequirements);
}

[[nodiscard]] inline bool deserialize_VkSparseImageMemoryRequirements2(::lsw::protocol::BufferReader& r, VkSparseImageMemoryRequirements2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkSparseImageMemoryRequirements(r, &out->memoryRequirements, arena)) { return false; }
    return true;
}

inline void serialize_VkSparseImageOpaqueMemoryBindInfo(::lsw::protocol::Buffer& buf, const VkSparseImageOpaqueMemoryBindInfo& v) {
    serialize_VkImage(buf, v.image);
    buf.write_pod(v.bindCount);
    {
        const std::uint8_t present = v.pBinds != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.bindCount); ++i) {
                serialize_VkSparseMemoryBind(buf, v.pBinds[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSparseImageOpaqueMemoryBindInfo(::lsw::protocol::BufferReader& r, VkSparseImageOpaqueMemoryBindInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!deserialize_VkImage(r, out->image)) { return false; }
    if (!r.read_pod(out->bindCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->bindCount == 0) {
            out->pBinds = nullptr;
        } else {
            auto* arr = arena.alloc<VkSparseMemoryBind>(static_cast<std::size_t>(out->bindCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->bindCount); ++i) {
                if (!deserialize_VkSparseMemoryBind(r, &arr[i], arena)) { return false; }
            }
            out->pBinds = arr;
        }
    }
    return true;
}

inline void serialize_VkSparseMemoryBind(::lsw::protocol::Buffer& buf, const VkSparseMemoryBind& v) {
    buf.write_pod(v.resourceOffset);
    buf.write_pod(v.size);
    serialize_VkDeviceMemory(buf, v.memory);
    buf.write_pod(v.memoryOffset);
    buf.write_pod(v.flags);
}

[[nodiscard]] inline bool deserialize_VkSparseMemoryBind(::lsw::protocol::BufferReader& r, VkSparseMemoryBind* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->resourceOffset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    if (!deserialize_VkDeviceMemory(r, out->memory)) { return false; }
    if (!r.read_pod(out->memoryOffset)) { return false; }
    if (!r.read_pod(out->flags)) { return false; }
    return true;
}

inline void serialize_VkSpecializationInfo(::lsw::protocol::Buffer& buf, const VkSpecializationInfo& v) {
    buf.write_pod(v.mapEntryCount);
    {
        const std::uint8_t present = v.pMapEntries != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.mapEntryCount); ++i) {
                serialize_VkSpecializationMapEntry(buf, v.pMapEntries[i]);
            }
        }
    }
    buf.write_pod(v.dataSize);
    // TODO: unhandled member shape 'unknown_ptr' for 'pData' (const void*)
    (void)v.pData;
}

[[nodiscard]] inline bool deserialize_VkSpecializationInfo(::lsw::protocol::BufferReader& r, VkSpecializationInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->mapEntryCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->mapEntryCount == 0) {
            out->pMapEntries = nullptr;
        } else {
            auto* arr = arena.alloc<VkSpecializationMapEntry>(static_cast<std::size_t>(out->mapEntryCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->mapEntryCount); ++i) {
                if (!deserialize_VkSpecializationMapEntry(r, &arr[i], arena)) { return false; }
            }
            out->pMapEntries = arr;
        }
    }
    if (!r.read_pod(out->dataSize)) { return false; }
    // TODO: unhandled member shape 'unknown_ptr' for 'pData' (const void*)
    (void)out->pData;
    return true;
}

inline void serialize_VkSpecializationMapEntry(::lsw::protocol::Buffer& buf, const VkSpecializationMapEntry& v) {
    buf.write_pod(v.constantID);
    buf.write_pod(v.offset);
    buf.write_pod(v.size);
}

[[nodiscard]] inline bool deserialize_VkSpecializationMapEntry(::lsw::protocol::BufferReader& r, VkSpecializationMapEntry* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->constantID)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    return true;
}

inline void serialize_VkSubmitInfo(::lsw::protocol::Buffer& buf, const VkSubmitInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.waitSemaphoreCount);
    {
        const std::uint8_t present = v.pWaitSemaphores != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.waitSemaphoreCount); ++i) {
                serialize_VkSemaphore(buf, v.pWaitSemaphores[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pWaitDstStageMask != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.waitSemaphoreCount); ++i) {
                buf.write_pod(v.pWaitDstStageMask[i]);
            }
        }
    }
    buf.write_pod(v.commandBufferCount);
    {
        const std::uint8_t present = v.pCommandBuffers != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.commandBufferCount); ++i) {
                serialize_VkCommandBuffer(buf, v.pCommandBuffers[i]);
            }
        }
    }
    buf.write_pod(v.signalSemaphoreCount);
    {
        const std::uint8_t present = v.pSignalSemaphores != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.signalSemaphoreCount); ++i) {
                serialize_VkSemaphore(buf, v.pSignalSemaphores[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSubmitInfo(::lsw::protocol::BufferReader& r, VkSubmitInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->waitSemaphoreCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->waitSemaphoreCount == 0) {
            out->pWaitSemaphores = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphore>(static_cast<std::size_t>(out->waitSemaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->waitSemaphoreCount); ++i) {
                if (!deserialize_VkSemaphore(r, arr[i])) { return false; }
            }
            out->pWaitSemaphores = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->waitSemaphoreCount == 0) {
            out->pWaitDstStageMask = nullptr;
        } else {
            auto* arr = arena.alloc<VkPipelineStageFlags>(static_cast<std::size_t>(out->waitSemaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->waitSemaphoreCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pWaitDstStageMask = arr;
        }
    }
    if (!r.read_pod(out->commandBufferCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->commandBufferCount == 0) {
            out->pCommandBuffers = nullptr;
        } else {
            auto* arr = arena.alloc<VkCommandBuffer>(static_cast<std::size_t>(out->commandBufferCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->commandBufferCount); ++i) {
                if (!deserialize_VkCommandBuffer(r, arr[i])) { return false; }
            }
            out->pCommandBuffers = arr;
        }
    }
    if (!r.read_pod(out->signalSemaphoreCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->signalSemaphoreCount == 0) {
            out->pSignalSemaphores = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphore>(static_cast<std::size_t>(out->signalSemaphoreCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->signalSemaphoreCount); ++i) {
                if (!deserialize_VkSemaphore(r, arr[i])) { return false; }
            }
            out->pSignalSemaphores = arr;
        }
    }
    return true;
}

inline void serialize_VkSubmitInfo2(::lsw::protocol::Buffer& buf, const VkSubmitInfo2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.waitSemaphoreInfoCount);
    {
        const std::uint8_t present = v.pWaitSemaphoreInfos != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.waitSemaphoreInfoCount); ++i) {
                serialize_VkSemaphoreSubmitInfo(buf, v.pWaitSemaphoreInfos[i]);
            }
        }
    }
    buf.write_pod(v.commandBufferInfoCount);
    {
        const std::uint8_t present = v.pCommandBufferInfos != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.commandBufferInfoCount); ++i) {
                serialize_VkCommandBufferSubmitInfo(buf, v.pCommandBufferInfos[i]);
            }
        }
    }
    buf.write_pod(v.signalSemaphoreInfoCount);
    {
        const std::uint8_t present = v.pSignalSemaphoreInfos != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.signalSemaphoreInfoCount); ++i) {
                serialize_VkSemaphoreSubmitInfo(buf, v.pSignalSemaphoreInfos[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSubmitInfo2(::lsw::protocol::BufferReader& r, VkSubmitInfo2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->waitSemaphoreInfoCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->waitSemaphoreInfoCount == 0) {
            out->pWaitSemaphoreInfos = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphoreSubmitInfo>(static_cast<std::size_t>(out->waitSemaphoreInfoCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->waitSemaphoreInfoCount); ++i) {
                if (!deserialize_VkSemaphoreSubmitInfo(r, &arr[i], arena)) { return false; }
            }
            out->pWaitSemaphoreInfos = arr;
        }
    }
    if (!r.read_pod(out->commandBufferInfoCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->commandBufferInfoCount == 0) {
            out->pCommandBufferInfos = nullptr;
        } else {
            auto* arr = arena.alloc<VkCommandBufferSubmitInfo>(static_cast<std::size_t>(out->commandBufferInfoCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->commandBufferInfoCount); ++i) {
                if (!deserialize_VkCommandBufferSubmitInfo(r, &arr[i], arena)) { return false; }
            }
            out->pCommandBufferInfos = arr;
        }
    }
    if (!r.read_pod(out->signalSemaphoreInfoCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->signalSemaphoreInfoCount == 0) {
            out->pSignalSemaphoreInfos = nullptr;
        } else {
            auto* arr = arena.alloc<VkSemaphoreSubmitInfo>(static_cast<std::size_t>(out->signalSemaphoreInfoCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->signalSemaphoreInfoCount); ++i) {
                if (!deserialize_VkSemaphoreSubmitInfo(r, &arr[i], arena)) { return false; }
            }
            out->pSignalSemaphoreInfos = arr;
        }
    }
    return true;
}

inline void serialize_VkSubpassBeginInfo(::lsw::protocol::Buffer& buf, const VkSubpassBeginInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.contents);
}

[[nodiscard]] inline bool deserialize_VkSubpassBeginInfo(::lsw::protocol::BufferReader& r, VkSubpassBeginInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->contents)) { return false; }
    return true;
}

inline void serialize_VkSubpassDependency(::lsw::protocol::Buffer& buf, const VkSubpassDependency& v) {
    buf.write_pod(v.srcSubpass);
    buf.write_pod(v.dstSubpass);
    buf.write_pod(v.srcStageMask);
    buf.write_pod(v.dstStageMask);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstAccessMask);
    buf.write_pod(v.dependencyFlags);
}

[[nodiscard]] inline bool deserialize_VkSubpassDependency(::lsw::protocol::BufferReader& r, VkSubpassDependency* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->srcSubpass)) { return false; }
    if (!r.read_pod(out->dstSubpass)) { return false; }
    if (!r.read_pod(out->srcStageMask)) { return false; }
    if (!r.read_pod(out->dstStageMask)) { return false; }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    if (!r.read_pod(out->dependencyFlags)) { return false; }
    return true;
}

inline void serialize_VkSubpassDependency2(::lsw::protocol::Buffer& buf, const VkSubpassDependency2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.srcSubpass);
    buf.write_pod(v.dstSubpass);
    buf.write_pod(v.srcStageMask);
    buf.write_pod(v.dstStageMask);
    buf.write_pod(v.srcAccessMask);
    buf.write_pod(v.dstAccessMask);
    buf.write_pod(v.dependencyFlags);
    buf.write_pod(v.viewOffset);
}

[[nodiscard]] inline bool deserialize_VkSubpassDependency2(::lsw::protocol::BufferReader& r, VkSubpassDependency2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->srcSubpass)) { return false; }
    if (!r.read_pod(out->dstSubpass)) { return false; }
    if (!r.read_pod(out->srcStageMask)) { return false; }
    if (!r.read_pod(out->dstStageMask)) { return false; }
    if (!r.read_pod(out->srcAccessMask)) { return false; }
    if (!r.read_pod(out->dstAccessMask)) { return false; }
    if (!r.read_pod(out->dependencyFlags)) { return false; }
    if (!r.read_pod(out->viewOffset)) { return false; }
    return true;
}

inline void serialize_VkSubpassDescription(::lsw::protocol::Buffer& buf, const VkSubpassDescription& v) {
    buf.write_pod(v.flags);
    buf.write_pod(v.pipelineBindPoint);
    buf.write_pod(v.inputAttachmentCount);
    {
        const std::uint8_t present = v.pInputAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.inputAttachmentCount); ++i) {
                serialize_VkAttachmentReference(buf, v.pInputAttachments[i]);
            }
        }
    }
    buf.write_pod(v.colorAttachmentCount);
    {
        const std::uint8_t present = v.pColorAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.colorAttachmentCount); ++i) {
                serialize_VkAttachmentReference(buf, v.pColorAttachments[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pResolveAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.colorAttachmentCount); ++i) {
                serialize_VkAttachmentReference(buf, v.pResolveAttachments[i]);
            }
        }
    }
    {
        const auto* p = v.pDepthStencilAttachment;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkAttachmentReference(buf, *p);
        }
    }
    buf.write_pod(v.preserveAttachmentCount);
    {
        const std::uint8_t present = v.pPreserveAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.preserveAttachmentCount); ++i) {
                buf.write_pod(v.pPreserveAttachments[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSubpassDescription(::lsw::protocol::BufferReader& r, VkSubpassDescription* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->pipelineBindPoint)) { return false; }
    if (!r.read_pod(out->inputAttachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->inputAttachmentCount == 0) {
            out->pInputAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentReference>(static_cast<std::size_t>(out->inputAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->inputAttachmentCount); ++i) {
                if (!deserialize_VkAttachmentReference(r, &arr[i], arena)) { return false; }
            }
            out->pInputAttachments = arr;
        }
    }
    if (!r.read_pod(out->colorAttachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->colorAttachmentCount == 0) {
            out->pColorAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentReference>(static_cast<std::size_t>(out->colorAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->colorAttachmentCount); ++i) {
                if (!deserialize_VkAttachmentReference(r, &arr[i], arena)) { return false; }
            }
            out->pColorAttachments = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->colorAttachmentCount == 0) {
            out->pResolveAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentReference>(static_cast<std::size_t>(out->colorAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->colorAttachmentCount); ++i) {
                if (!deserialize_VkAttachmentReference(r, &arr[i], arena)) { return false; }
            }
            out->pResolveAttachments = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pDepthStencilAttachment = nullptr;
        } else {
            auto* p = arena.alloc<VkAttachmentReference>();
            if (!deserialize_VkAttachmentReference(r, p, arena)) { return false; }
            out->pDepthStencilAttachment = p;
        }
    }
    if (!r.read_pod(out->preserveAttachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->preserveAttachmentCount == 0) {
            out->pPreserveAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->preserveAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->preserveAttachmentCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pPreserveAttachments = arr;
        }
    }
    return true;
}

inline void serialize_VkSubpassDescription2(::lsw::protocol::Buffer& buf, const VkSubpassDescription2& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    buf.write_pod(v.pipelineBindPoint);
    buf.write_pod(v.viewMask);
    buf.write_pod(v.inputAttachmentCount);
    {
        const std::uint8_t present = v.pInputAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.inputAttachmentCount); ++i) {
                serialize_VkAttachmentReference2(buf, v.pInputAttachments[i]);
            }
        }
    }
    buf.write_pod(v.colorAttachmentCount);
    {
        const std::uint8_t present = v.pColorAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.colorAttachmentCount); ++i) {
                serialize_VkAttachmentReference2(buf, v.pColorAttachments[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pResolveAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.colorAttachmentCount); ++i) {
                serialize_VkAttachmentReference2(buf, v.pResolveAttachments[i]);
            }
        }
    }
    {
        const auto* p = v.pDepthStencilAttachment;
        if (p == nullptr) {
            buf.write_pod<std::uint8_t>(0);
        } else {
            buf.write_pod<std::uint8_t>(1);
            serialize_VkAttachmentReference2(buf, *p);
        }
    }
    buf.write_pod(v.preserveAttachmentCount);
    {
        const std::uint8_t present = v.pPreserveAttachments != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.preserveAttachmentCount); ++i) {
                buf.write_pod(v.pPreserveAttachments[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkSubpassDescription2(::lsw::protocol::BufferReader& r, VkSubpassDescription2* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!r.read_pod(out->pipelineBindPoint)) { return false; }
    if (!r.read_pod(out->viewMask)) { return false; }
    if (!r.read_pod(out->inputAttachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->inputAttachmentCount == 0) {
            out->pInputAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentReference2>(static_cast<std::size_t>(out->inputAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->inputAttachmentCount); ++i) {
                if (!deserialize_VkAttachmentReference2(r, &arr[i], arena)) { return false; }
            }
            out->pInputAttachments = arr;
        }
    }
    if (!r.read_pod(out->colorAttachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->colorAttachmentCount == 0) {
            out->pColorAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentReference2>(static_cast<std::size_t>(out->colorAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->colorAttachmentCount); ++i) {
                if (!deserialize_VkAttachmentReference2(r, &arr[i], arena)) { return false; }
            }
            out->pColorAttachments = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->colorAttachmentCount == 0) {
            out->pResolveAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<VkAttachmentReference2>(static_cast<std::size_t>(out->colorAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->colorAttachmentCount); ++i) {
                if (!deserialize_VkAttachmentReference2(r, &arr[i], arena)) { return false; }
            }
            out->pResolveAttachments = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (present == 0) {
            out->pDepthStencilAttachment = nullptr;
        } else {
            auto* p = arena.alloc<VkAttachmentReference2>();
            if (!deserialize_VkAttachmentReference2(r, p, arena)) { return false; }
            out->pDepthStencilAttachment = p;
        }
    }
    if (!r.read_pod(out->preserveAttachmentCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->preserveAttachmentCount == 0) {
            out->pPreserveAttachments = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->preserveAttachmentCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->preserveAttachmentCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pPreserveAttachments = arr;
        }
    }
    return true;
}

inline void serialize_VkSubpassEndInfo(::lsw::protocol::Buffer& buf, const VkSubpassEndInfo& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
}

[[nodiscard]] inline bool deserialize_VkSubpassEndInfo(::lsw::protocol::BufferReader& r, VkSubpassEndInfo* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    return true;
}

inline void serialize_VkSubresourceLayout(::lsw::protocol::Buffer& buf, const VkSubresourceLayout& v) {
    buf.write_pod(v.offset);
    buf.write_pod(v.size);
    buf.write_pod(v.rowPitch);
    buf.write_pod(v.arrayPitch);
    buf.write_pod(v.depthPitch);
}

[[nodiscard]] inline bool deserialize_VkSubresourceLayout(::lsw::protocol::BufferReader& r, VkSubresourceLayout* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->offset)) { return false; }
    if (!r.read_pod(out->size)) { return false; }
    if (!r.read_pod(out->rowPitch)) { return false; }
    if (!r.read_pod(out->arrayPitch)) { return false; }
    if (!r.read_pod(out->depthPitch)) { return false; }
    return true;
}

inline void serialize_VkSurfaceCapabilitiesKHR(::lsw::protocol::Buffer& buf, const VkSurfaceCapabilitiesKHR& v) {
    buf.write_pod(v.minImageCount);
    buf.write_pod(v.maxImageCount);
    serialize_VkExtent2D(buf, v.currentExtent);
    serialize_VkExtent2D(buf, v.minImageExtent);
    serialize_VkExtent2D(buf, v.maxImageExtent);
    buf.write_pod(v.maxImageArrayLayers);
    buf.write_pod(v.supportedTransforms);
    buf.write_pod(v.currentTransform);
    buf.write_pod(v.supportedCompositeAlpha);
    buf.write_pod(v.supportedUsageFlags);
}

[[nodiscard]] inline bool deserialize_VkSurfaceCapabilitiesKHR(::lsw::protocol::BufferReader& r, VkSurfaceCapabilitiesKHR* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->minImageCount)) { return false; }
    if (!r.read_pod(out->maxImageCount)) { return false; }
    if (!deserialize_VkExtent2D(r, &out->currentExtent, arena)) { return false; }
    if (!deserialize_VkExtent2D(r, &out->minImageExtent, arena)) { return false; }
    if (!deserialize_VkExtent2D(r, &out->maxImageExtent, arena)) { return false; }
    if (!r.read_pod(out->maxImageArrayLayers)) { return false; }
    if (!r.read_pod(out->supportedTransforms)) { return false; }
    if (!r.read_pod(out->currentTransform)) { return false; }
    if (!r.read_pod(out->supportedCompositeAlpha)) { return false; }
    if (!r.read_pod(out->supportedUsageFlags)) { return false; }
    return true;
}

inline void serialize_VkSurfaceFormatKHR(::lsw::protocol::Buffer& buf, const VkSurfaceFormatKHR& v) {
    buf.write_pod(v.format);
    buf.write_pod(v.colorSpace);
}

[[nodiscard]] inline bool deserialize_VkSurfaceFormatKHR(::lsw::protocol::BufferReader& r, VkSurfaceFormatKHR* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->colorSpace)) { return false; }
    return true;
}

inline void serialize_VkSwapchainCreateInfoKHR(::lsw::protocol::Buffer& buf, const VkSwapchainCreateInfoKHR& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    buf.write_pod(v.flags);
    serialize_VkSurfaceKHR(buf, v.surface);
    buf.write_pod(v.minImageCount);
    buf.write_pod(v.imageFormat);
    buf.write_pod(v.imageColorSpace);
    serialize_VkExtent2D(buf, v.imageExtent);
    buf.write_pod(v.imageArrayLayers);
    buf.write_pod(v.imageUsage);
    buf.write_pod(v.imageSharingMode);
    buf.write_pod(v.queueFamilyIndexCount);
    {
        const std::uint8_t present = v.pQueueFamilyIndices != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.queueFamilyIndexCount); ++i) {
                buf.write_pod(v.pQueueFamilyIndices[i]);
            }
        }
    }
    buf.write_pod(v.preTransform);
    buf.write_pod(v.compositeAlpha);
    buf.write_pod(v.presentMode);
    buf.write_pod(v.clipped);
    serialize_VkSwapchainKHR(buf, v.oldSwapchain);
}

[[nodiscard]] inline bool deserialize_VkSwapchainCreateInfoKHR(::lsw::protocol::BufferReader& r, VkSwapchainCreateInfoKHR* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!r.read_pod(out->flags)) { return false; }
    if (!deserialize_VkSurfaceKHR(r, out->surface)) { return false; }
    if (!r.read_pod(out->minImageCount)) { return false; }
    if (!r.read_pod(out->imageFormat)) { return false; }
    if (!r.read_pod(out->imageColorSpace)) { return false; }
    if (!deserialize_VkExtent2D(r, &out->imageExtent, arena)) { return false; }
    if (!r.read_pod(out->imageArrayLayers)) { return false; }
    if (!r.read_pod(out->imageUsage)) { return false; }
    if (!r.read_pod(out->imageSharingMode)) { return false; }
    if (!r.read_pod(out->queueFamilyIndexCount)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->queueFamilyIndexCount == 0) {
            out->pQueueFamilyIndices = nullptr;
        } else {
            auto* arr = arena.alloc<uint32_t>(static_cast<std::size_t>(out->queueFamilyIndexCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->queueFamilyIndexCount); ++i) {
                if (!r.read_pod(arr[i])) { return false; }
            }
            out->pQueueFamilyIndices = arr;
        }
    }
    if (!r.read_pod(out->preTransform)) { return false; }
    if (!r.read_pod(out->compositeAlpha)) { return false; }
    if (!r.read_pod(out->presentMode)) { return false; }
    if (!r.read_pod(out->clipped)) { return false; }
    if (!deserialize_VkSwapchainKHR(r, out->oldSwapchain)) { return false; }
    return true;
}

inline void serialize_VkVertexInputAttributeDescription(::lsw::protocol::Buffer& buf, const VkVertexInputAttributeDescription& v) {
    buf.write_pod(v.location);
    buf.write_pod(v.binding);
    buf.write_pod(v.format);
    buf.write_pod(v.offset);
}

[[nodiscard]] inline bool deserialize_VkVertexInputAttributeDescription(::lsw::protocol::BufferReader& r, VkVertexInputAttributeDescription* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->location)) { return false; }
    if (!r.read_pod(out->binding)) { return false; }
    if (!r.read_pod(out->format)) { return false; }
    if (!r.read_pod(out->offset)) { return false; }
    return true;
}

inline void serialize_VkVertexInputBindingDescription(::lsw::protocol::Buffer& buf, const VkVertexInputBindingDescription& v) {
    buf.write_pod(v.binding);
    buf.write_pod(v.stride);
    buf.write_pod(v.inputRate);
}

[[nodiscard]] inline bool deserialize_VkVertexInputBindingDescription(::lsw::protocol::BufferReader& r, VkVertexInputBindingDescription* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->binding)) { return false; }
    if (!r.read_pod(out->stride)) { return false; }
    if (!r.read_pod(out->inputRate)) { return false; }
    return true;
}

inline void serialize_VkViewport(::lsw::protocol::Buffer& buf, const VkViewport& v) {
    buf.write_pod(v.x);
    buf.write_pod(v.y);
    buf.write_pod(v.width);
    buf.write_pod(v.height);
    buf.write_pod(v.minDepth);
    buf.write_pod(v.maxDepth);
}

[[nodiscard]] inline bool deserialize_VkViewport(::lsw::protocol::BufferReader& r, VkViewport* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->x)) { return false; }
    if (!r.read_pod(out->y)) { return false; }
    if (!r.read_pod(out->width)) { return false; }
    if (!r.read_pod(out->height)) { return false; }
    if (!r.read_pod(out->minDepth)) { return false; }
    if (!r.read_pod(out->maxDepth)) { return false; }
    return true;
}

inline void serialize_VkWriteDescriptorSet(::lsw::protocol::Buffer& buf, const VkWriteDescriptorSet& v) {
    buf.write_pod(v.sType);
    // pNext: empty chain in Phase 1; future phases serialize the chain length and its links.
    buf.write_varint(0);
    serialize_VkDescriptorSet(buf, v.dstSet);
    buf.write_pod(v.dstBinding);
    buf.write_pod(v.dstArrayElement);
    buf.write_pod(v.descriptorCount);
    buf.write_pod(v.descriptorType);
    {
        const std::uint8_t present = v.pImageInfo != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.descriptorCount); ++i) {
                serialize_VkDescriptorImageInfo(buf, v.pImageInfo[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pBufferInfo != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.descriptorCount); ++i) {
                serialize_VkDescriptorBufferInfo(buf, v.pBufferInfo[i]);
            }
        }
    }
    {
        const std::uint8_t present = v.pTexelBufferView != nullptr ? 1 : 0;
        buf.write_pod(present);
        if (present) {
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v.descriptorCount); ++i) {
                serialize_VkBufferView(buf, v.pTexelBufferView[i]);
            }
        }
    }
}

[[nodiscard]] inline bool deserialize_VkWriteDescriptorSet(::lsw::protocol::BufferReader& r, VkWriteDescriptorSet* out, ::lsw::protocol::Arena& arena) {
    (void)arena;
    if (!r.read_pod(out->sType)) { return false; }
    {
        std::uint64_t pnext_count = 0;
        if (!r.read_varint(pnext_count)) { return false; }
        if (pnext_count != 0) { return false; }  // Phase 1: no pNext extensions
    }
    if (!deserialize_VkDescriptorSet(r, out->dstSet)) { return false; }
    if (!r.read_pod(out->dstBinding)) { return false; }
    if (!r.read_pod(out->dstArrayElement)) { return false; }
    if (!r.read_pod(out->descriptorCount)) { return false; }
    if (!r.read_pod(out->descriptorType)) { return false; }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->descriptorCount == 0) {
            out->pImageInfo = nullptr;
        } else {
            auto* arr = arena.alloc<VkDescriptorImageInfo>(static_cast<std::size_t>(out->descriptorCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->descriptorCount); ++i) {
                if (!deserialize_VkDescriptorImageInfo(r, &arr[i], arena)) { return false; }
            }
            out->pImageInfo = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->descriptorCount == 0) {
            out->pBufferInfo = nullptr;
        } else {
            auto* arr = arena.alloc<VkDescriptorBufferInfo>(static_cast<std::size_t>(out->descriptorCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->descriptorCount); ++i) {
                if (!deserialize_VkDescriptorBufferInfo(r, &arr[i], arena)) { return false; }
            }
            out->pBufferInfo = arr;
        }
    }
    {
        std::uint8_t present = 0;
        if (!r.read_pod(present)) { return false; }
        if (!present || out->descriptorCount == 0) {
            out->pTexelBufferView = nullptr;
        } else {
            auto* arr = arena.alloc<VkBufferView>(static_cast<std::size_t>(out->descriptorCount));
            for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(out->descriptorCount); ++i) {
                if (!deserialize_VkBufferView(r, arr[i])) { return false; }
            }
            out->pTexelBufferView = arr;
        }
    }
    return true;
}

}  // namespace lsw::vkproto
