#pragma once

#include "lsw/icd/entry_table.hpp"

#include <span>

namespace lsw::icd::surface {

// Entry points implemented client-side because their create-info
// carries process-local pointers that can't cross a vsock boundary
// (xcb_connection_t*, wl_display*). The wire path is a custom
// ``kCmdCreateXcbSurface`` frame; the server creates the real
// ``VkSurfaceKHR`` against its own xcb connection.
//
// Names provided:
//   * vkCreateXcbSurfaceKHR
//   * vkDestroySurfaceKHR
//   * vkGetPhysicalDeviceXcbPresentationSupportKHR
//
// Wayland / xlib aren't here yet — the server doesn't have a non-xcb
// window backend.
[[nodiscard]] std::span<const Entry> entries() noexcept;

}  // namespace lsw::icd::surface
