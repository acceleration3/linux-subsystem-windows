#pragma once

#include "lsw/icd/memory_shadow.hpp"
#include "lsw/protocol/region_arena.hpp"
#include "lsw/protocol/sub_region.hpp"
#include "lsw/protocol/transport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace lsw::icd {

// Per-VkInstance connection. Owns its own ``Transport`` and the cached
// HELLO_ACK fields. One Connection is created on ``vkCreateInstance`` and
// destroyed on ``vkDestroyInstance``; child dispatchable handles
// (VkPhysicalDevice / VkDevice / VkQueue / VkCommandBuffer) keep a
// non-owning back-pointer.
//
// Replaces the process-wide UnixSocketTransport singleton — multiple
// VkInstances in the same process now have independent state.
class Connection {
public:
    Connection() = default;
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    // Connect to ``endpoint`` and run the HELLO handshake. ``app_name`` is
    // a short identifier (e.g. ``/proc/self/comm``) sent in HELLO. Returns
    // 0 on success and a negative value on failure.
    [[nodiscard]] int open(std::string_view endpoint, std::string_view app_name);

    // Effectively-zero-config: opens to ``$LSW_SERVER_SOCKET`` (default
    // ``unix:///tmp/lsw.sock``) using ``/proc/self/comm`` as the app name.
    [[nodiscard]] int open_default();

    [[nodiscard]] bool is_open() const noexcept { return transport_ != nullptr; }

    [[nodiscard]] ::lsw::protocol::Transport& transport() noexcept { return *transport_; }

    [[nodiscard]] std::uint64_t context_id() const noexcept { return context_id_; }

    [[nodiscard]] const std::string& app_name() const noexcept { return app_name_; }

    // Per-instance host-visible memory mirror. Used by memory.cpp to
    // back vkMapMemory.
    [[nodiscard]] MemoryShadow& memory_shadow() noexcept { return shadow_; }

    // The region's hostmem arena, when this connection shares memory
    // with the server at all.
    //
    // ``bound()`` is false over a socket, and that is the only test
    // anything performs before trying an import: sharing memory requires
    // both peers to have the same pages mapped, and a transport either
    // gives you that or it does not. Nothing here asks whether there is
    // a VM.
    [[nodiscard]] ::lsw::protocol::RegionArena& host_memory_arena() noexcept { return arena_; }

private:
    std::unique_ptr<::lsw::protocol::Transport> transport_;
    // The window onto the region's hostmem channel, and the allocator
    // over it. Empty unless the transport is region-backed.
    std::shared_ptr<::lsw::protocol::SharedRegion> region_;
    ::lsw::protocol::SubRegion hostmem_window_;
    ::lsw::protocol::RegionArena arena_;
    std::uint64_t context_id_ = 0;
    std::string app_name_;
    MemoryShadow shadow_;
};

// Factory: create + open a Connection using the same defaults as
// ``Connection::open_default``. Returns nullptr on failure (logged to
// stderr). The caller becomes the owner — call ``release_connection``
// to destroy it.
[[nodiscard]] Connection* acquire_default_connection();

// Tear down a Connection acquired via ``acquire_default_connection``.
// Safe to pass nullptr.
void release_connection(Connection* c) noexcept;

}  // namespace lsw::icd
