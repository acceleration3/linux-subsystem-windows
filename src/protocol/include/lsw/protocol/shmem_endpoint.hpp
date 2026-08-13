#pragma once

#include "lsw/protocol/listener.hpp"
#include "lsw/protocol/posix_shm_region.hpp"
#include "lsw/protocol/shmem_frame_channel.hpp"

#include "lsw/linkproto/region.hpp"
#include "lsw/linkproto/region_layout.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace lsw::protocol {

// Region size for a locally created region: the same constant the
// manager gives a VM, not a smaller one that would fit a test. The
// channel table is a pure function of this number, so a different size
// here would mean the loopback and the guest lay out different regions —
// and everything would still work, differently, which is the failure
// that is hardest to see.
inline constexpr std::uint64_t kDefaultShmRegionBytes = linkproto::kDefaultRegionBytes;

// The server end of a shared-memory link.
//
// "Accept" is a borrowed word here. Shared memory has no connection to
// accept — the region either exists or it does not — so this creates the
// region, lays out the channel table exactly as the manager does for a
// VM, and hands back a server-role channel bound to ``cmdstream``. The
// peer attaches whenever it likes, by name, and the first thing it
// writes is the first thing the channel reads.
//
// One peer at a time, by construction: a channel is a single-producer
// ring pair. A second ``accept`` re-initialises the rings and yields a
// fresh channel, which is how a second application run gets a clean
// stream after the first has gone.
class ShmemListener final : public Listener {
public:
    ShmemListener() = default;
    ~ShmemListener() override;

    // Create ``/name`` of ``size_bytes`` and lay out its channels.
    // Returns 0, or a negative errno.
    [[nodiscard]] int create(const std::string& name,
                             std::uint64_t size_bytes = kDefaultShmRegionBytes);

    // Attach to a region SOMEBODY ELSE created and laid out, and serve
    // its cmdstream channel.
    //
    // This is the VM case, and it is not the same as ``create``. For a
    // guest, the manager owns the region: it sizes it, writes the
    // channel table, hands the fd to QEMU, and unlinks it on exit. A
    // render server that created its own region would be serving a
    // second, unrelated piece of memory that no guest is attached to —
    // which is precisely the shape of "everything works and nothing
    // renders".
    //
    // So this opens the existing name, refuses anything that is not a
    // laid-out LSW region of this version, and never unlinks: the name
    // belongs to whoever made it.
    //
    // Returns 0, -ENOENT when the name does not exist, or -EPROTO when
    // what is there is not a region this build understands.
    [[nodiscard]] int attach(const std::string& name);

    [[nodiscard]] std::unique_ptr<FrameChannel> accept() override;
    void close() noexcept override;
    [[nodiscard]] std::string description() const override;

    [[nodiscard]] std::shared_ptr<SharedRegion> backing_region() const override {
        return region_;
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::shared_ptr<PosixShmRegion> region_;
    std::string name_;
    std::atomic<bool> closed_{false};
};

// Attach to a region another process created, by name only — no fd, no
// pointer, no shared heap. That restriction is the point: it is the
// guest's situation exactly, so a pointer stored in the region or a
// layout assumption that only held within one address space fails here,
// on this machine, instead of in a VM.
//
// Waits up to ``timeout`` for the region to appear AND to hold a
// readable channel table; a region that exists but is still being laid
// out is not ready, and the two are indistinguishable from the outside.
[[nodiscard]] std::unique_ptr<ShmemFrameChannel> attach_shmem_channel(
    const std::string& name, std::uint32_t channel_kind = linkproto::kChannelCmdStream,
    std::chrono::milliseconds timeout = std::chrono::seconds{5});

}  // namespace lsw::protocol
