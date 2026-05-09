#pragma once

#include <cstdint>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace lsw::server {

// Server-side handle remap. Most Vulkan handles cross the wire as the
// real driver-issued value (the client just echoes them back) — so this
// table is intentionally small. It holds entries only for handle types
// whose wire value is *synthetic* on the client side and must be
// translated to the real driver handle here. In Phase 2 the only such
// type is ``VkSurfaceKHR``: the client's ICD intercepts
// ``vkCreateXxxSurfaceKHR`` and asks the server to make a host window;
// the server stores ``synthetic_id -> real VkSurfaceKHR`` here.
//
// Phase 2 ships an empty implementation (no surface support yet); the
// surface intercept lands in Phase 2.5 alongside Wayland windowing.
class HandleTable {
public:
    // ---- Surface ----
    // ``client_id`` is whatever 64-bit value the client minted when its
    // Win32/Xcb/Wayland surface intercept fired. The real ``VkSurfaceKHR``
    // is whatever the server's window backend returned.
    void register_surface(std::uint64_t client_id, VkSurfaceKHR real) noexcept;
    [[nodiscard]] VkSurfaceKHR resolve_surface(std::uint64_t client_id) const noexcept;
    void forget_surface(std::uint64_t client_id) noexcept;

private:
    std::unordered_map<std::uint64_t, VkSurfaceKHR> surfaces_;
};

}  // namespace lsw::server
