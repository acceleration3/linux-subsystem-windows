#include "lsw/server/handle_table.hpp"

namespace lsw::server {

void HandleTable::register_surface(std::uint64_t client_id, VkSurfaceKHR real) noexcept {
    surfaces_[client_id] = real;
}

VkSurfaceKHR HandleTable::resolve_surface(std::uint64_t client_id) const noexcept {
    const auto it = surfaces_.find(client_id);
    return it == surfaces_.end() ? VK_NULL_HANDLE : it->second;
}

void HandleTable::forget_surface(std::uint64_t client_id) noexcept {
    surfaces_.erase(client_id);
}

}  // namespace lsw::server
