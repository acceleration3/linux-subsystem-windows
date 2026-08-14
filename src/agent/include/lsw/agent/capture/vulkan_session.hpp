#pragma once

namespace lsw::agent::capture {

// Who owns the IVSHMEM mapping while a Vulkan application runs.
//
// The driver gives the BAR to one process at a time, and an application
// rendering through the LSW ICD needs it: that region is the command
// stream. The agent therefore steps aside for the duration — it
// releases the mapping before the process starts and takes it back when
// the process exits.
//
// Stepping aside is not the same as dying. The control plane keeps
// working over TCP, so launches, window enumeration and everything else
// the manager asks for stay available while the game runs; what stops
// is the frame path (nothing to mirror through) and the shared-memory
// RPC route.
//
// This allows exactly ONE Vulkan application at a time, which is the
// driver's limit rather than a choice. Lifting it means giving each
// application a region of its own — a pool of ivshmem devices — and is
// deliberately not what this does.
class VulkanSession {
public:
    // Release the region so a launch can take it. Returns false if the
    // agent could not let go, in which case the caller should launch
    // anyway: the application will fail to reach the host and fall back
    // to reporting no Vulkan driver, which is better than not starting.
    [[nodiscard]] static bool begin_launch();

    // Watch ``pid`` and take the region back when it exits. Detached:
    // the RPC that launched the application has long since replied.
    static void watch(unsigned long pid);

    // The launch failed after ``begin_launch`` — take the region back
    // now, since nothing is going to use it.
    static void abandon_launch();

    // True while a launched Vulkan application is being waited on.
    [[nodiscard]] static bool active();
};

}  // namespace lsw::agent::capture
