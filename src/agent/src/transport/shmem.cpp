#include "lsw/agent/transport/shmem.hpp"

#include "lsw/agent/log.hpp"

#include "lsw/linkproto/frame_ring.hpp"
#include "lsw/agent/transport/ivshmem_region.hpp"

#include <atomic>
#include <cstring>

namespace lsw::agent::transport {

namespace {

// The region layout, the seqlock protocol and the state/format values
// come from the shared frame-ring contract — the same header the manager
// compiles. This file used to keep its own list of magic offsets, which
// had already drifted from the manager's copy.
namespace slot_off = linkproto::slot_off;
namespace hdr_off = linkproto::hdr_off;
namespace region_off = linkproto::region_off;
using linkproto::load_field;
using linkproto::store_field;
using linkproto::store_seqno_release;

}  // namespace

Shmem& shmem() {
    static Shmem g;
    return g;
}

bool Shmem::open() {
    if (base_ != nullptr) {
        return true;
    }
    // The mapping is ``IvshmemRegion`` — the same class
    // the guest ICD uses, and the same interface the host side satisfies
    // with POSIX shared memory. This file used to open the device and
    // drive the IOCTLs itself, which meant two implementations of the
    // one thing rule 1 allows to differ between local and VM. There is
    // now one, and the agent and the ICD share it.
    if (const int rc = region_.open(); rc != 0) {
        switch (rc) {
        case -ENODEV:
            LSW_AGENT_WARN("shmem: no IVSHMEM device (driver not installed yet?)");
            break;
        case -EPROTO:
            LSW_AGENT_WARN("shmem: region header mismatch — manager and agent are "
                           "different builds? (region format v{})",
                           linkproto::kRegionVersion);
            break;
        default:
            // The OS error, not just the errno-style code: the driver
            // answers ERROR_ALREADY_INITIALIZED, ACCESS_DENIED and
            // INVALID_FUNCTION for three different causes, and
            // collapsing them into one line cost a debugging round trip
            // once already.
            LSW_AGENT_WARN("shmem: could not map the IVSHMEM BAR (rc={}, GetLastError={})",
                           rc, region_.last_os_error());
            break;
        }
        return false;
    }

    const auto frames = linkproto::find_channel(region_.base(), linkproto::kChannelFrames);
    if (!frames.valid()) {
        LSW_AGENT_WARN("shmem: region has no frame channel");
        region_.close();
        return false;
    }

    // The frame channel carries its own header; slot and buffer offsets
    // inside it are relative to the channel, not the region.
    auto* frames_base = static_cast<std::uint8_t*>(region_.base()) + frames.offset;
    const auto slot_count = load_field<std::uint32_t>(frames_base + hdr_off::slot_count);
    const auto frame_version = load_field<std::uint32_t>(frames_base + hdr_off::version);
    if (slot_count != kSlotCount || frame_version != linkproto::kFrameRingVersion) {
        LSW_AGENT_WARN("shmem: frame channel mismatch slots={} ver={}", slot_count,
                       frame_version);
        region_.close();
        return false;
    }

    base_ = region_.base();
    size_ = region_.size();
    frames_base_ = frames_base;
    frames_size_ = frames.size;
    buffers_offset_ = load_field<std::uint64_t>(frames_base + hdr_off::buffers_offset);
    buffers_size_ = load_field<std::uint64_t>(frames_base + hdr_off::buffers_size);

    bind_duplex_channel(linkproto::kChannelRpc, rpc_to_host_, rpc_from_host_);

    LSW_AGENT_INFO("shmem: opened (size={} MiB, frames={} MiB, buffers={} MiB, "
                   "rpc_ring={} KiB, peerID={}, vectors={})",
                   size_ / (1024 * 1024), frames_size_ / (1024 * 1024),
                   buffers_size_ / (1024 * 1024), rpc_to_host_.capacity() / 1024,
                   region_.peer_id(), region_.msi_vectors());
    if (region_.msi_vectors() < linkproto::kDoorbellVectorCount) {
        // Frames still work (their vectors come first); the channel
        // doorbells silently would not, so say so once rather than leave
        // a channel that looks alive but never wakes the manager.
        LSW_AGENT_WARN("shmem: device exposes {} MSI vectors, need {} for channel "
                       "doorbells — messages will only be seen when the manager polls",
                       region_.msi_vectors(), linkproto::kDoorbellVectorCount);
    }
    return true;
}

void Shmem::bind_duplex_channel(std::uint32_t kind, linkproto::ByteRingProducer& to_host,
                                 linkproto::ByteRingConsumer& from_host) {
    const auto channel = linkproto::find_channel(base_, kind);
    if (!channel.valid()) {
        // An older manager simply has no such channel. Callers check
        // ``valid()`` and fall back to the socket.
        return;
    }
    auto* channel_base = static_cast<std::uint8_t*>(base_) + channel.offset;
    to_host = linkproto::ByteRingProducer{
        channel_base + linkproto::guest_to_host_ring_offset(channel.size)};
    from_host = linkproto::ByteRingConsumer{
        channel_base + linkproto::host_to_guest_ring_offset(channel.size)};
}

void Shmem::ring_doorbell(std::uint16_t vector) {
    region_.ring_vector(vector);
}

void Shmem::ring_rpc_doorbell() {
    ring_doorbell(linkproto::kRpcDoorbellVector);
}

Shmem::SlotInfo Shmem::alloc_slot(std::uint64_t hwnd, std::uint32_t width,
                                   std::uint32_t height) {
    SlotInfo out;
    if (base_ == nullptr || width == 0 || height == 0) {
        return out;
    }
    // BGRA stride. The shmem-buffer half is sized exactly to one
    // tightly-packed frame (no padding); the agent will write at this
    // stride too. WGC's mapped staging texture may have its own
    // RowPitch, but ``commit_frame`` handles that with a per-row copy.
    const std::uint32_t stride = width * 4U;
    const std::uint64_t one_buf = static_cast<std::uint64_t>(stride) * height;
    // Round up to 64-byte alignment so each slot's halves are
    // cache-line aligned. Helps WC-memory write performance.
    const std::uint64_t aligned_buf = (one_buf + 63ULL) & ~63ULL;
    const std::uint64_t pair_size = aligned_buf * 2ULL;  // front + back

    // Best-fit allocator. Find an unused reservation whose size >=
    // pair_size (recycled), or carve out a fresh region after the
    // last reservation. Falls back to compaction if neither works.
    int best = -1;
    int free_slot = -1;
    std::uint64_t high_water = buffers_offset_;
    for (int i = 0; i < static_cast<int>(kSlotCount); ++i) {
        if (!reservations_[i].in_use) {
            if (free_slot < 0) free_slot = i;
            if (reservations_[i].size >= pair_size
                && (best < 0
                    || reservations_[i].size < reservations_[best].size)) {
                best = i;
            }
            continue;
        }
        const std::uint64_t end = reservations_[i].offset + reservations_[i].size;
        if (end > high_water) high_water = end;
    }
    int slot_idx = -1;
    std::uint64_t off = 0;
    if (best >= 0) {
        slot_idx = best;
        off = reservations_[best].offset;
    } else if (free_slot >= 0
               && high_water + pair_size <= buffers_offset_ + buffers_size_) {
        slot_idx = free_slot;
        off = high_water;
    } else {
        LSW_AGENT_WARN("shmem: alloc_slot failed (no room for "
                       + std::to_string(pair_size / 1024) + " KiB)");
        return out;
    }

    reservations_[slot_idx] = {off, pair_size, true};
    out.index       = slot_idx;
    out.buffer_off  = off;
    out.buffer_size = pair_size;
    out.stride      = stride;
    out.width       = width;
    out.height      = height;

    // Initialize the slot table entry so the manager finds it active
    // on the first doorbell.
    auto* slot = linkproto::slot_ptr(frames_base_, static_cast<std::uint32_t>(slot_idx));
    store_field<std::uint64_t>(slot + slot_off::hwnd,          hwnd);
    store_field<std::uint32_t>(slot + slot_off::format,        kFormatBgra8Premul);
    store_field<std::uint32_t>(slot + slot_off::width,         width);
    store_field<std::uint32_t>(slot + slot_off::stride,        stride);
    store_field<std::uint32_t>(slot + slot_off::height,        height);
    store_field<std::uint64_t>(slot + slot_off::buffer_offset, off);
    store_field<std::uint64_t>(slot + slot_off::buffer_size,   pair_size);
    store_field<std::uint64_t>(slot + slot_off::seqno,         0ULL);
    store_field<std::uint64_t>(slot + slot_off::present_qpc,   0ULL);
    std::atomic_thread_fence(std::memory_order_release);
    store_field<std::uint32_t>(slot + slot_off::state, linkproto::kSlotStateActive);
    return out;
}

void Shmem::free_slot(int slot_index) {
    if (base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return;
    }
    auto* slot = linkproto::slot_ptr(frames_base_, static_cast<std::uint32_t>(slot_index));
    // Mark empty before releasing the reservation so a racing manager
    // read returns false rather than reading a half-released slot.
    store_field<std::uint32_t>(slot + slot_off::state, linkproto::kSlotStateEmpty);
    store_field<std::uint64_t>(slot + slot_off::hwnd,  0ULL);
    reservations_[slot_index].in_use = false;
    // Keep .offset/.size so a same-or-smaller realloc reuses the
    // exact memory range (alloc's best-fit will pick it).
}

bool Shmem::commit_frame(int slot_index, const std::uint8_t* src_bgra,
                          std::uint32_t src_pitch, std::uint32_t width,
                          std::uint32_t height, std::uint64_t present_qpc,
                          std::uint32_t visible_insets) {
    if (base_ == nullptr || slot_index < 0
        || slot_index >= static_cast<int>(kSlotCount)) {
        return false;
    }
    auto& r = reservations_[slot_index];
    if (!r.in_use) {
        return false;
    }
    const std::uint32_t stride = width * 4U;
    const std::uint64_t one_buf = static_cast<std::uint64_t>(stride) * height;
    const std::uint64_t half = r.size / 2ULL;
    if (one_buf > half) {
        LSW_AGENT_WARN("shmem: commit_frame slot=" + std::to_string(slot_index)
                       + " size grew: " + std::to_string(one_buf) + " > "
                       + std::to_string(half));
        return false;
    }
    auto* slot = linkproto::slot_ptr(frames_base_, static_cast<std::uint32_t>(slot_index));

    // Seqlock: mark mid-write, fill the half the manager is NOT reading,
    // then commit — at which point that half becomes the readable front.
    // The arithmetic lives in the shared contract so both peers agree on
    // what the low bits mean.
    const std::uint64_t committed = load_field<std::uint64_t>(slot + slot_off::seqno);
    const std::uint64_t next = linkproto::seqno_next_commit(committed);

    store_seqno_release(slot + slot_off::seqno, linkproto::seqno_begin_write(next));
    const std::uint64_t back_off = r.offset + linkproto::half_offset(next, r.size);
    std::uint8_t* dst = frames_base_ + back_off;
    if (src_pitch == stride) {
        std::memcpy(dst, src_bgra, one_buf);
    } else {
        for (std::uint32_t y = 0; y < height; ++y) {
            std::memcpy(dst + static_cast<std::uint64_t>(y) * stride,
                        src_bgra + static_cast<std::uint64_t>(y) * src_pitch,
                        stride);
        }
    }
    store_field<std::uint64_t>(slot + slot_off::present_qpc, present_qpc);
    // Update geometry every frame (window may have been resize-reallocated
    // by the allocator, but a same-size resize without a free/alloc bump
    // is also valid — width/height stay in sync).
    store_field<std::uint32_t>(slot + slot_off::width,  width);
    store_field<std::uint32_t>(slot + slot_off::stride, stride);
    store_field<std::uint32_t>(slot + slot_off::height, height);
    // Inside the seqlock with the pixels it describes — that pairing is
    // the whole reason this rides here instead of on the enumeration.
    store_field<std::uint32_t>(slot + slot_off::visible_insets, visible_insets);
    store_seqno_release(slot + slot_off::seqno, next);  // commit

    // Ring the manager's doorbell on this slot's vector. Frame slots own
    // vectors 0..kSlotCount-1; the message channels have their own past
    // the end (see linkproto/region.hpp).
    ring_doorbell(static_cast<std::uint16_t>(slot_index));
    return true;
}

}  // namespace lsw::agent::transport
