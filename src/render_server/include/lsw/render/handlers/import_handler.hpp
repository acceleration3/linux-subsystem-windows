#pragma once

#include "lsw/protocol/shared_region.hpp"
#include "lsw/render/dispatch/translating_dispatch.hpp"
#include "lsw/render/handler.hpp"

#include <memory>

namespace lsw::render::handlers {

// Allocates VkDeviceMemory that IS the client's memory, rather than a
// copy of it.
//
// The client puts a host-visible allocation in the region's hostmem
// arena and sends its offset; both peers have those pages mapped, so
// this end can hand the driver its own view of them through
// ``VK_EXT_external_memory_host``. From then on the application writes
// through a mapped pointer that the GPU is already reading — there is
// nothing to flush, and the submit-time copy of every mapped range
// stops happening for that allocation.
//
// Every reason it might not work is decided HERE, at runtime, per
// allocation:
//
//   * the region is not shared with this peer (a socket client)
//   * the device does not support VK_EXT_external_memory_host
//   * the host pointer does not meet the driver's
//     minImportedHostPointerAlignment
//   * the driver refuses the import for its own reasons
//
// and all four produce the same answer — ``imported = 0`` — which the
// client treats as "use the copy path". None of those facts reaches the
// client, the wire or the region layout, so a machine where the import
// never succeeds renders exactly as it does today.
class ImportHandler final : public Handler {
public:
    ImportHandler(std::shared_ptr<::lsw::protocol::SharedRegion> region,
                  dispatch::TranslatingDispatch& dispatch) noexcept
        : region_(std::move(region)), dispatch_(dispatch) {}

    [[nodiscard]] HandleResult handle(Session& session, std::uint32_t cmd_id,
                                      std::span<const std::uint8_t> payload,
                                      ::lsw::protocol::Buffer& reply) override;

private:
    // Null when the client is not sharing memory with us at all.
    std::shared_ptr<::lsw::protocol::SharedRegion> region_;
    // Knows which physical device each VkDevice came from, which is
    // where the alignment requirement has to be read from.
    dispatch::TranslatingDispatch& dispatch_;
};

}  // namespace lsw::render::handlers
