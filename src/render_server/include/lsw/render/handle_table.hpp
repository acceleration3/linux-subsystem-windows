#pragma once

#include <cstdint>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace lsw::render {

// Server-side handle remap. Most Vulkan handles cross the wire as the
// real driver-issued value (the client just echoes them back) — so this
// table is intentionally small. It holds entries only for handle types
// whose wire value is *synthetic* on the client side and must be
// translated to the real driver handle here. In Phase 2 the only such
// type is ``VkSurfaceKHR``: the client's ICD intercepts
// ``vkCreateXxxSurfaceKHR`` and asks the server to make a host window;
// the server stores ``synthetic_id -> real VkSurfaceKHR`` here.
//
// THREADING: unsynchronised, and correct that way. One HandleTable
// belongs to one ``Session``, which belongs to one connection, which is
// reached only from that connection's single replay thread. See
// ``session.hpp`` for why replay is single-threaded by requirement.
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

}  // namespace lsw::render
