#include "lsw/icd/queue.hpp"

#include "lsw/icd/connection.hpp"
#include "lsw/icd/handle_wrap.hpp"
#include "lsw/icd/memory_shadow.hpp"
#include "vk_icd_entries.h"  // generated::Wrap_vk*

#include <array>
#include <vulkan/vulkan_core.h>

namespace lsw::icd::queue {

namespace {

// Ship every live mapping before the GPU reads it. See the header for
// why this is not the application's job.
void flush_mapped_before_submit(VkQueue queue) {
    auto* conn = ::lsw::icd::connection_of(queue);
    if (conn == nullptr) {
        return;
    }
    // A failure here is not worth failing the submit over: the submit
    // would fail anyway if the data really was needed, and returning an
    // error the app cannot act on turns a rendering glitch into a
    // crash. flush_range already logs.
    (void)conn->memory_shadow().flush_all_mapped(conn->transport());
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue, uint32_t submitCount,
                                           const VkSubmitInfo* pSubmits, VkFence fence) {
    flush_mapped_before_submit(queue);
    return ::lsw::icd::generated::Wrap_vkQueueSubmit(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue queue, uint32_t submitCount,
                                            const VkSubmitInfo2* pSubmits, VkFence fence) {
    flush_mapped_before_submit(queue);
    return ::lsw::icd::generated::Wrap_vkQueueSubmit2(queue, submitCount, pSubmits, fence);
}

// vkQueueSubmit2KHR aliases vkQueueSubmit2 in the generated table, so
// both names route to the same wrapper.
const std::array<Entry, 3> kEntries = {{
    {"vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmit)},
    {"vkQueueSubmit2", reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmit2)},
    {"vkQueueSubmit2KHR", reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmit2)},
}};

}  // namespace

std::span<const Entry> entries() noexcept {
    return {kEntries.data(), kEntries.size()};
}

}  // namespace lsw::icd::queue
