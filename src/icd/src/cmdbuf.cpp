#include "lsw/icd/cmdbuf.hpp"

#include "lsw/base/log.hpp"
#include "lsw/icd/command_recorder.hpp"
#include "lsw/icd/connection.hpp"
#include "lsw/icd/handle_wrap.hpp"
#include "vk_icd_entries.h"  // generated::Wrap_vk*

#include <array>
#include <vulkan/vulkan_core.h>

namespace lsw::icd {

::lsw::protocol::Transport& record_target(VkCommandBuffer wrapped) noexcept {
    if (auto* rec = recorder_of(wrapped); rec != nullptr) {
        return *rec;
    }
    // Only reachable if a command buffer arrived from somewhere that did
    // not wrap it. Sending immediately is the old behaviour: slower, but
    // correct, and it keeps a bug in handle wrapping from presenting as
    // a crash inside every draw call.
    LSW_WARN("command buffer has no recorder; falling back to direct send");
    return connection_of(wrapped)->transport();
}

namespace cmdbuf {

namespace {

VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                  const VkCommandBufferBeginInfo* pBeginInfo) {
    // Drop first, then record. Anything still in the recorder belongs to
    // a previous recording of this buffer that was abandoned — after a
    // pool reset, or after the app simply started over. Begin means "the
    // contents are gone", and the Begin itself is the first thing in the
    // new batch rather than a message of its own.
    if (auto* rec = recorder_of(commandBuffer); rec != nullptr) {
        rec->reset();
    }
    return generated::Wrap_vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL EndCommandBuffer(VkCommandBuffer commandBuffer) {
    auto* conn = connection_of(commandBuffer);
    // Record the End, THEN ship. The batch that reaches the server is a
    // complete recording — Begin, the commands, End — replayed in order,
    // in one message, with no round trip anywhere in it.
    //
    // "Recorded before submitted" still holds for free: no submit can
    // name this buffer until End has returned, and End does not return
    // until the batch is away.
    const VkResult rc = generated::Wrap_vkEndCommandBuffer(commandBuffer);
    if (auto* rec = recorder_of(commandBuffer); rec != nullptr && conn != nullptr) {
        if (rec->flush(conn->transport()) != ::lsw::protocol::TransportResult::Ok) {
            LSW_ERROR("failed to flush recorded commands for command buffer {}",
                      static_cast<const void*>(commandBuffer));
            return VK_ERROR_DEVICE_LOST;
        }
    }
    return rc;
}

VKAPI_ATTR VkResult VKAPI_CALL ResetCommandBuffer(VkCommandBuffer commandBuffer,
                                                  VkCommandBufferResetFlags flags) {
    if (auto* rec = recorder_of(commandBuffer); rec != nullptr) {
        rec->reset();
    }
    return generated::Wrap_vkResetCommandBuffer(commandBuffer, flags);
}

VKAPI_ATTR void VKAPI_CALL FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                                              uint32_t commandBufferCount,
                                              const VkCommandBuffer* pCommandBuffers) {
    generated::Wrap_vkFreeCommandBuffers(device, commandPool, commandBufferCount,
                                         pCommandBuffers);
    // After the wire call, which still needs the wrappers in order to
    // unwrap the handles it sends. The generated wrapper never destroyed
    // them — a leak of one wrapper per freed buffer that went unnoticed
    // while a wrapper was three words; each one now owns a recorder with
    // a payload buffer attached, which is not a size worth leaking.
    if (pCommandBuffers == nullptr) {
        return;
    }
    for (std::uint32_t i = 0; i < commandBufferCount; ++i) {
        destroy_wrapper(pCommandBuffers[i]);
    }
}

const std::array<Entry, 4> kEntries = {{
    {"vkBeginCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(&BeginCommandBuffer)},
    {"vkEndCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(&EndCommandBuffer)},
    {"vkResetCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(&ResetCommandBuffer)},
    {"vkFreeCommandBuffers", reinterpret_cast<PFN_vkVoidFunction>(&FreeCommandBuffers)},
}};

}  // namespace

std::span<const Entry> entries() noexcept {
    return {kEntries.data(), kEntries.size()};
}

}  // namespace cmdbuf
}  // namespace lsw::icd
