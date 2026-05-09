#pragma once

#include "lsw/icd/entry_table.hpp"

#include <span>

namespace lsw::icd::loader {

// Loader-negotiation entry points: vkEnumerateInstance{Version,
// ExtensionProperties,LayerProperties} and vkGet{Instance,Device}ProcAddr.
// Implementations live in icd_entry.cpp alongside the extern "C" exports
// the loader actually dlsyms.
[[nodiscard]] std::span<const Entry> entries() noexcept;

}  // namespace lsw::icd::loader
