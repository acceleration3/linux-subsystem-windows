#include "lsw/protocol/shmem_endpoint.hpp"

#include "lsw/linkproto/region_layout.hpp"

#include <cerrno>
#include <thread>
#include <utility>

namespace lsw::protocol {

namespace {

// How long to wait between attempts while the peer's region appears.
constexpr auto kAttachPollInterval = std::chrono::milliseconds{10};

}  // namespace

ShmemListener::~ShmemListener() {
    close();
}

int ShmemListener::create(const std::string& name, std::uint64_t size_bytes) {
    close();
    auto region = std::make_shared<PosixShmRegion>();
    if (const int rc = region->create(name, static_cast<std::size_t>(size_bytes)); rc != 0) {
        return rc;
    }
    // The same call the manager makes for a VM. Not a similar one — the
    // table a local peer reads and the table a guest reads come out of
    // one function, which is what the parity test asserts.
    const auto layout = linkproto::lay_out_region(region->base(), size_bytes);
    if (!layout.valid) {
        return -ENOSPC;
    }
    region_ = std::move(region);
    name_ = name;
    closed_.store(false);
    return 0;
}

int ShmemListener::attach(const std::string& name) {
    close();
    auto region = std::make_shared<PosixShmRegion>();
    if (region->open(name) != 0) {
        return -ENOENT;
    }
    // Do NOT lay out: the owner already did, and rewriting the table
    // under a live guest would move channels out from under it.
    if (!linkproto::region_is_readable(region->base(),
                                       static_cast<std::uint64_t>(region->size()))) {
        return -EPROTO;
    }
    region_ = std::move(region);
    name_ = name;
    closed_.store(false);
    return 0;
}

std::unique_ptr<FrameChannel> ShmemListener::accept() {
    if (region_ == nullptr || closed_.load()) {
        return nullptr;
    }
    auto channel = std::make_unique<ShmemFrameChannel>();
    if (channel->bind_channel(region_, linkproto::kChannelCmdStream,
                              ShmemFrameChannel::Role::Server)
        != 0) {
        return nullptr;
    }
    return channel;
}

void ShmemListener::close() noexcept {
    closed_.store(true);
    // Channels handed out hold their own reference to the mapping, so
    // dropping ours here unmaps only when the last one is gone.
    //
    // Whether the NAME goes away depends on how we got it:
    // PosixShmRegion unlinks only what it created. An attached region
    // belongs to the manager and outlives us, which is the point.
    region_.reset();
    name_.clear();
}

std::string ShmemListener::description() const {
    return "shm://" + name_;
}

std::unique_ptr<ShmemFrameChannel> attach_shmem_channel(const std::string& name,
                                                        std::uint32_t channel_kind,
                                                        std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        auto region = std::make_shared<PosixShmRegion>();
        if (region->open(name) == 0
            && linkproto::region_is_readable(region->base(),
                                             static_cast<std::uint64_t>(region->size()))) {
            auto channel = std::make_unique<ShmemFrameChannel>();
            if (channel->bind_channel(std::move(region), channel_kind,
                                      ShmemFrameChannel::Role::Client)
                == 0) {
                return channel;
            }
        }
        // Either the region is not there yet or its table is still being
        // written. Both look the same from here and both are fixed by
        // waiting, so there is nothing to distinguish.
        if (std::chrono::steady_clock::now() >= deadline) {
            return nullptr;
        }
        std::this_thread::sleep_for(kAttachPollInterval);
    }
}

}  // namespace lsw::protocol
