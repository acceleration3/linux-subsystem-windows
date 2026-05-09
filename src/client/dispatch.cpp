#include "lsw/icd/dispatch.hpp"

#include "lsw/icd/entry_table.hpp"
#include "lsw/icd/loader.hpp"
#include "lsw/icd/memory.hpp"
#include "lsw/icd/surface.hpp"
#include "lsw/log.hpp"
#include "vk_icd_entries.h"  // generated wrapper_entries()

#include <cstring>
#include <initializer_list>
#include <span>
#include <vulkan/vulkan_core.h>

namespace lsw::icd {

namespace {

[[nodiscard]] PFN_vkVoidFunction find_in(std::span<const Entry> entries,
                                         const char* name) noexcept {
    for (const auto& e : entries) {
        if (std::strcmp(name, e.name) == 0) {
            return e.fn;
        }
    }
    return nullptr;
}

}  // namespace

PFN_vkVoidFunction lookup_entry(const char* pName) noexcept {
    if (pName == nullptr) {
        return nullptr;
    }
    // Modules are walked in priority order: loader contract first
    // (its names must beat anything generated), then per-feature
    // modules whose entries override the codegen-emitted wrappers
    // for the same name. The codegen is the catch-all at the end.
    const std::initializer_list<std::span<const Entry>> sources = {
        ::lsw::icd::loader::entries(),
        ::lsw::icd::surface::entries(),
        ::lsw::icd::memory::entries(),
    };
    for (auto src : sources) {
        if (auto fn = find_in(src, pName); fn != nullptr) {
            return fn;
        }
    }
    for (const auto& e : ::lsw::icd::generated::wrapper_entries()) {
        if (std::strcmp(pName, e.name) == 0) {
            return e.fn;
        }
    }
    LSW_TRACE("lookup_entry MISS {}", pName);
    return nullptr;
}

}  // namespace lsw::icd
