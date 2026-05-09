#include "lsw/server/handlers/memory_handler.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <cstdint>
#include <cstring>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace lsw::server::handlers {

namespace {

template<typename Handle>
[[nodiscard]] Handle unpack(std::uint64_t raw) noexcept {
    Handle out{};
    static_assert(sizeof(out) == sizeof(raw));
    std::memcpy(&out, &raw, sizeof(out));
    return out;
}

[[nodiscard]] bool read_header(::lsw::protocol::BufferReader& r, std::uint64_t& device,
                               std::uint64_t& memory, std::uint64_t& offset,
                               std::uint64_t& size) noexcept {
    return r.read_pod(device) && r.read_pod(memory) && r.read_pod(offset) && r.read_pod(size);
}

[[nodiscard]] HandleResult handle_write(::lsw::protocol::BufferReader& r,
                                        ::lsw::protocol::Buffer& reply) {
    std::uint64_t device_raw = 0;
    std::uint64_t memory_raw = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    if (!read_header(r, device_raw, memory_raw, offset, size)) {
        return HandleResult::ProtocolError;
    }
    if (r.remaining() < size) {
        return HandleResult::ProtocolError;
    }
    const VkDevice device = unpack<VkDevice>(device_raw);
    const VkDeviceMemory memory = unpack<VkDeviceMemory>(memory_raw);

    void* dst = nullptr;
    const VkResult map_rc = ::vkMapMemory(device, memory, offset, size, 0, &dst);
    if (map_rc != VK_SUCCESS) {
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(map_rc));
        return HandleResult::Handled;
    }
    if (!r.read(dst, static_cast<std::size_t>(size))) {
        ::vkUnmapMemory(device, memory);
        return HandleResult::ProtocolError;
    }
    ::vkUnmapMemory(device, memory);
    reply.write_pod<std::int32_t>(VK_SUCCESS);
    return HandleResult::Handled;
}

[[nodiscard]] HandleResult handle_read(::lsw::protocol::BufferReader& r,
                                       ::lsw::protocol::Buffer& reply) {
    std::uint64_t device_raw = 0;
    std::uint64_t memory_raw = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    if (!read_header(r, device_raw, memory_raw, offset, size)) {
        return HandleResult::ProtocolError;
    }
    const VkDevice device = unpack<VkDevice>(device_raw);
    const VkDeviceMemory memory = unpack<VkDeviceMemory>(memory_raw);

    void* src = nullptr;
    const VkResult map_rc = ::vkMapMemory(device, memory, offset, size, 0, &src);
    if (map_rc != VK_SUCCESS) {
        reply.write_pod<std::int32_t>(static_cast<std::int32_t>(map_rc));
        return HandleResult::Handled;
    }
    reply.write_pod<std::int32_t>(VK_SUCCESS);
    reply.write(src, static_cast<std::size_t>(size));
    ::vkUnmapMemory(device, memory);
    return HandleResult::Handled;
}

}  // namespace

HandleResult MemoryHandler::handle(Session& /*session*/, std::uint32_t cmd_id,
                                   std::span<const std::uint8_t> payload,
                                   ::lsw::protocol::Buffer& reply) {
    ::lsw::protocol::BufferReader r{payload};
    if (cmd_id == ::lsw::protocol::kCmdMemoryWrite) {
        return handle_write(r, reply);
    }
    if (cmd_id == ::lsw::protocol::kCmdMemoryRead) {
        return handle_read(r, reply);
    }
    return HandleResult::Pass;
}

}  // namespace lsw::server::handlers
