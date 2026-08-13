#include "lsw/protocol/shmem_frame_channel.hpp"

#include "lsw/linkproto/region.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::protocol {

namespace {

// One frame is one ring record. The ring already delimits records and
// guarantees a reader never sees half of one, so there is no length
// field here — a second one would only be a way for the two framings to
// disagree.
//
//   [cmd_id:u32][flags:u8][reply_token:u64][payload]
//
// The token is unconditional, unlike the socket header where it is
// present only when ``needs_reply`` is set. Eight bytes are not worth a
// branch when the record is memcpy'd anyway, and it makes replies —
// which carry a token regardless of flags — the same shape as requests.
constexpr std::size_t kCmdIdSize = sizeof(std::uint32_t);
constexpr std::size_t kFlagsSize = sizeof(std::uint8_t);
constexpr std::size_t kTokenSize = sizeof(std::uint64_t);
constexpr std::size_t kRecordHeaderSize = kCmdIdSize + kFlagsSize + kTokenSize;

static_assert(kRecordHeaderSize == 13, "frame record header is a wire contract");

// How long a poll waits before looking at the ring again.
//
// The cap is the single number that decides latency here, and it has to
// be two numbers rather than one.
//
// NEITHER DIRECTION HAS A USABLE INTERRUPT. The guest's MSI-X doorbell
// wakes the manager's ivshmem server, not this channel's peer, and
// there is no doorbell at all from host to guest. So every synchronous
// Vulkan call waits out a poll interval on each leg, twice per round
// trip.
//
// Measured, guest vkcube through the real VM:
//     4000us cap ->  46 fps   (visibly stuttery)
//      150us cap ->  93 fps
//
// But a short cap applied unconditionally is what made an idle server
// hold a slice of a core forever. So: short while traffic is recent,
// long once the channel has genuinely gone quiet. An application that
// is rendering never sees the long one; a server nobody is talking to
// never sees the short one.
constexpr auto kMinPollSlice = std::chrono::microseconds{50};
constexpr auto kActivePollSlice = std::chrono::microseconds{150};
constexpr auto kIdlePollSlice = std::chrono::microseconds{4000};
// How long after the last traffic the channel still counts as active.
// Comfortably longer than a frame, so a 60 Hz application stays in the
// fast tier across its own inter-frame gaps.
constexpr auto kActiveWindow = std::chrono::milliseconds{250};

[[nodiscard]] std::chrono::microseconds next_slice(std::chrono::microseconds current,
                                                   std::chrono::microseconds cap) noexcept {
    return std::min(current * 2, cap);
}

}  // namespace

int ShmemFrameChannel::bind(SharedRegion& region, Role role, Doorbell& doorbell) {
    if (!region.valid()) {
        return -EINVAL;
    }
    auto* base = static_cast<std::uint8_t*>(region.base());
    const auto size = static_cast<std::uint64_t>(region.size());

    // Direction names in the layout are absolute — "guest to host" —
    // never relative to the speaker, so both ends compute the same two
    // offsets and simply take opposite roles. Client is the side the
    // guest runs (the ICD); locally it is the second process, which is
    // the same thing minus a hypervisor.
    auto* guest_to_host = base + linkproto::guest_to_host_ring_offset(size);
    auto* host_to_guest = base + linkproto::host_to_guest_ring_offset(size);

    if (role == Role::Client) {
        out_ = linkproto::ByteRingProducer{guest_to_host};
        in_ = linkproto::ByteRingConsumer{host_to_guest};
    } else {
        out_ = linkproto::ByteRingProducer{host_to_guest};
        in_ = linkproto::ByteRingConsumer{guest_to_host};
    }

    // ``ByteRingEndpoint`` refuses a capacity that is not a power of two,
    // which is what an uninitialised region looks like. Reporting that as
    // EINVAL here beats computing wrong indices into it forever.
    if (!out_.valid() || !in_.valid()) {
        out_ = {};
        in_ = {};
        return -EINVAL;
    }

    doorbell_ = &doorbell;
    bound_ = true;
    closed_.store(false);
    read_shutdown_.store(false);
    read_scratch_.assign(in_.max_message(), 0);
    write_scratch_.clear();
    description_ = "shm://" + region.description()
                   + (role == Role::Client ? "#client" : "#server");
    return 0;
}

int ShmemFrameChannel::bind_channel(std::shared_ptr<SharedRegion> region,
                                    std::uint32_t channel_kind, Role role, Doorbell* doorbell) {
    if (region == nullptr || !region->valid()) {
        return -EINVAL;
    }
    // By kind, never by offset: the side that owns the layout can move
    // this channel and rewrite one table entry, and neither peer has to
    // learn about it.
    const auto view = linkproto::find_channel(region->base(), channel_kind);
    if (!view.valid()) {
        return -ENOENT;
    }
    if (!window_.bind(*region, view.offset, view.size)) {
        // The table describes a channel that does not fit the mapping.
        return -EFAULT;
    }
    const int rc = bind(window_, role, doorbell != nullptr ? *doorbell : own_doorbell_);
    if (rc != 0) {
        return rc;
    }
    owned_region_ = std::move(region);
    return 0;
}

int ShmemFrameChannel::init_region(SharedRegion& region) {
    if (!region.valid()) {
        return -EINVAL;
    }
    auto* base = static_cast<std::uint8_t*>(region.base());
    const auto size = static_cast<std::uint64_t>(region.size());
    const std::uint64_t ring_bytes = linkproto::duplex_ring_bytes(size);
    if (!linkproto::ring_init(base + linkproto::guest_to_host_ring_offset(size), ring_bytes)
        || !linkproto::ring_init(base + linkproto::host_to_guest_ring_offset(size), ring_bytes)) {
        return -EINVAL;
    }
    return 0;
}

std::uint32_t ShmemFrameChannel::max_payload() const noexcept {
    if (!bound_) {
        return 0;
    }
    const std::uint32_t record = out_.max_message();
    return record <= kRecordHeaderSize ? 0
                                       : record - static_cast<std::uint32_t>(kRecordHeaderSize);
}

std::uint64_t ShmemFrameChannel::region_bytes_for_payload(std::uint32_t payload_bytes) {
    // Invert the chain: payload -> record -> ring max_message ->
    // capacity -> ring area -> channel. Each ring is half the channel and
    // ``ring_max_message`` is half its capacity minus a record header, so
    // a payload needs roughly four times its size in channel bytes, plus
    // the two control blocks.
    const std::uint64_t record = static_cast<std::uint64_t>(payload_bytes) + kRecordHeaderSize;
    const std::uint64_t capacity_needed = (record + linkproto::kRingRecordHeaderSize) * 2;
    std::uint64_t capacity = 64;
    while (capacity < capacity_needed) {
        capacity <<= 1;
    }
    return (capacity + sizeof(linkproto::ByteRingHeader)) * 2;
}

IoResult ShmemFrameChannel::read_any(Frame& out) noexcept {
    if (!bound_ || closed_.load()) {
        return IoResult::Disconnected;
    }
    const auto deadline = std::chrono::steady_clock::now() + read_timeout_;
    auto slice = kMinPollSlice;
    // Snapshot the send counter: any change means we are now waiting on
    // a reply rather than merely idling, and should poll hard again.
    auto seen_writes = writes_.load(std::memory_order_acquire);

    while (true) {
        if (closed_.load() || read_shutdown_.load()) {
            return IoResult::Disconnected;
        }
        const std::size_t n = in_.read(read_scratch_.data(), read_scratch_.size());
        if (n != 0) {
            if (n < kRecordHeaderSize) {
                // The ring delivered a whole record, so a record too
                // short to hold a header did not come from this code.
                return IoResult::InvalidFrame;
            }
            const std::uint8_t* p = read_scratch_.data();
            std::memcpy(&out.cmd_id, p, kCmdIdSize);
            out.flags = p[kCmdIdSize];
            std::memcpy(&out.reply_token, p + kCmdIdSize + kFlagsSize, kTokenSize);
            out.payload.assign(p + kRecordHeaderSize, p + n);
            last_activity_ = std::chrono::steady_clock::now();
            return IoResult::Ok;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return IoResult::Timeout;
        }
        // ``wait`` may return early and often; the loop re-checks the
        // ring rather than trusting a wakeup to mean data, which is what
        // makes a polling doorbell and an interrupt-backed one
        // interchangeable here.
        //
        // Spin only while the channel is still warm. Once the slice has
        // backed off to its maximum this reader has seen nothing for a
        // while, and spinning at that point is how an idle server ends
        // up holding 5% of a core indefinitely.
        // Went out while we were waiting? Then a reply is on its way and
        // this is the worst possible moment to be dozing.
        if (const auto now_writes = writes_.load(std::memory_order_acquire);
            now_writes != seen_writes) {
            seen_writes = now_writes;
            slice = kMinPollSlice;
            last_activity_ = std::chrono::steady_clock::now();
        }
        const bool active =
            (std::chrono::steady_clock::now() - last_activity_) < kActiveWindow;
        const auto cap = active ? kActivePollSlice : kIdlePollSlice;
        doorbell_->wait(std::min(slice, cap), /*allow_spin=*/active);
        slice = next_slice(slice, cap);
    }
}

IoResult ShmemFrameChannel::read_frame(Frame& out) noexcept {
    return read_any(out);
}

IoResult ShmemFrameChannel::read_reply(Frame& out) noexcept {
    // Same ring, same record shape. The socket needs two entry points
    // because a reply's header differs from a request's; here it does
    // not, and the difference survives only as a name.
    return read_any(out);
}

IoResult ShmemFrameChannel::write_record(std::uint32_t cmd_id, std::uint8_t flags,
                                         std::uint64_t reply_token,
                                         std::span<const std::uint8_t> payload) noexcept {
    if (!bound_ || closed_.load()) {
        return IoResult::Disconnected;
    }
    if (payload.size() > max_payload()) {
        // No amount of waiting makes this fit. Retrying to the timeout
        // would turn a caller's bug into a ten-second stall.
        return IoResult::InvalidFrame;
    }

    write_scratch_.resize(kRecordHeaderSize + payload.size());
    std::uint8_t* p = write_scratch_.data();
    std::memcpy(p, &cmd_id, kCmdIdSize);
    p[kCmdIdSize] = flags;
    std::memcpy(p + kCmdIdSize + kFlagsSize, &reply_token, kTokenSize);
    if (!payload.empty()) {
        std::memcpy(p + kRecordHeaderSize, payload.data(), payload.size());
    }

    const auto deadline = std::chrono::steady_clock::now() + write_timeout_;
    auto slice = kMinPollSlice;
    while (true) {
        if (out_.write(write_scratch_.data(), write_scratch_.size())) {
            doorbell_->ring();
            // Tell our own reader to go hot: this is a request whose
            // reply we are about to wait for.
            writes_.fetch_add(1, std::memory_order_release);
            return IoResult::Ok;
        }
        if (closed_.load()) {
            return IoResult::Disconnected;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            // The peer has not drained the ring in ``write_timeout_``.
            // That is a stalled or dead consumer, not congestion.
            return IoResult::Timeout;
        }
        // Ring the doorbell while waiting for room: the reason the ring
        // is full may be that the peer went to sleep without noticing the
        // last record.
        doorbell_->ring();
        // A full ring means the peer is behind, not idle, so stay in the
        // fast tier while waiting for room.
        doorbell_->wait(std::min(slice, kActivePollSlice), /*allow_spin=*/true);
        slice = next_slice(slice, kActivePollSlice);
    }
}

IoResult ShmemFrameChannel::write_frame(std::uint32_t cmd_id, std::uint8_t flags,
                                        std::uint64_t reply_token,
                                        std::span<const std::uint8_t> payload) noexcept {
    return write_record(cmd_id, flags, reply_token, payload);
}

IoResult ShmemFrameChannel::write_reply(const Frame& request, std::span<const std::uint8_t> payload,
                                        bool error) noexcept {
    // Bit 0 in a reply means ERROR, matching the socket. Replies always
    // carry the request's token.
    return write_record(request.cmd_id, error ? std::uint8_t{1} : std::uint8_t{0},
                        request.reply_token, payload);
}

void ShmemFrameChannel::shutdown_read() noexcept {
    read_shutdown_.store(true);
    if (doorbell_ != nullptr) {
        // A reader parked in ``wait`` polls on a bounded slice, so it
        // notices the flag on its own; ringing just shortens that.
        doorbell_->ring();
    }
}

void ShmemFrameChannel::close() noexcept {
    closed_.store(true);
    read_shutdown_.store(true);
    if (doorbell_ != nullptr) {
        doorbell_->ring();
    }
}

}  // namespace lsw::protocol
