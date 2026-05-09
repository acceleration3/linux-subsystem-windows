#pragma once

#include <span>
#include <vulkan/vulkan_core.h>

namespace lsw::icd {

// One ``vkXxx`` name -> function pointer mapping. Each ICD module
// (loader, surface, memory, ...) exposes a ``entries()`` function
// returning a ``span<const Entry>``; ``dispatch.cpp`` walks them in
// priority order, falling through to the codegen-emitted wrappers
// last.
struct Entry {
    const char* name;
    PFN_vkVoidFunction fn;
};

}  // namespace lsw::icd
