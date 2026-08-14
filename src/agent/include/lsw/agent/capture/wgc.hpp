#pragma once

#include <cstdint>
#include <string>

namespace lsw::agent::capture {

// Windows.Graphics.Capture backend. One persistent capture session
// per HWND is kept alive across frames (creating a session per frame
// is far too slow); ``tick_capture`` drains the WGC frame pool to
// the newest frame, copies its BGRA-premultiplied pixels into the
// per-window slot in the host-shared IVSHMEM region, and rings the
// MSI-X doorbell so the host manager picks them up.
//
// The capture transport runs entirely through ``transport::shmem``;
// there is no TCP frame envelope. If the IVSHMEM driver isn't
// installed or the region's LSWF header isn't ready, ``available``
// returns false and the caller must skip capture.
class Wgc {
public:
    // True iff the WGC runtime is functional AND the IVSHMEM region
    // is mapped + header-verified. Cached after first success.
    [[nodiscard]] static bool available();

    // Drain the WGC pool for ``hwnd`` and (if a new frame is queued)
    // commit it into the shmem slot for that window. Returns true if
    // the window is still alive (caller keeps the subscription) and
    // false if it has vanished (caller emits frame_gone + drops sub).
    [[nodiscard]] static bool tick_capture(std::uintptr_t hwnd);

    // Drop a window's session (window closed / unsubscribed). Frees
    // the associated shmem slot.
    static void release(std::uintptr_t hwnd);

    // Drop every capture session and free every slot it holds.
    //
    // For the moment the region goes away underneath us: a Vulkan
    // application is taking the mapping, and the sessions' slot indices
    // describe an allocator that will not exist. Frames stop until the
    // manager subscribes again, which is what it does when a mirror is
    // next created. Called BEFORE the region is released, because
    // freeing a slot writes to it.
    static void drop_all_sessions();

    // Diagnostic stub retained for the existing RPC handler; the
    // alpha-histogram path retired with the JPEG encode path.
    [[nodiscard]] static std::string debug_json(std::uintptr_t hwnd);
};

}  // namespace lsw::agent::capture
