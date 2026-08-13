#pragma once

#include <vulkan/vulkan_core.h>

namespace lsw::icd {

// Resolve a Vulkan function name to its ICD entry point. Walks the
// generated wrapper table (codegen-emitted vk_icd_entries) plus the
// small set of locally-handled entries that don't go on the wire
// (loader-negotiation, vkGet{Instance,Device}ProcAddr).
//
// Returns nullptr for unknown names. Also returns nullptr for ``pName ==
// nullptr``. The result is a stable pointer-to-function with VKAPI_CALL
// linkage and the public Vulkan signature for ``pName``.
PFN_vkVoidFunction lookup_entry(const char* pName) noexcept;

}  // namespace lsw::icd
