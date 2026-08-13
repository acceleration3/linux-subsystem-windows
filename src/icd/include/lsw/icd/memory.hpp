#pragma once

#include "lsw/icd/entry_table.hpp"

#include <span>

namespace lsw::icd::memory {

// Memory-related ICD entries. Some shadow the codegen wrappers
// (vkAllocateMemory / vkFreeMemory) so we can track allocation sizes
// for VK_WHOLE_SIZE resolution; others (vkMapMemory et al.) the codegen
// can't emit because their parameter shapes (void**) aren't handled yet
// — they're hand-written here against the per-Connection MemoryShadow.
//
// Entries provided:
//   * vkAllocateMemory                  (wraps generated::Wrap_vkAllocateMemory)
//   * vkFreeMemory                      (wraps generated::Wrap_vkFreeMemory)
//   * vkMapMemory                       (hand-written, MemoryShadow::map)
//   * vkUnmapMemory                     (hand-written, MemoryShadow::unmap_and_flush)
//   * vkFlushMappedMemoryRanges         (hand-written, MemoryShadow::flush_range)
//   * vkInvalidateMappedMemoryRanges    (hand-written, MemoryShadow::invalidate_range)
[[nodiscard]] std::span<const Entry> entries() noexcept;

}  // namespace lsw::icd::memory
