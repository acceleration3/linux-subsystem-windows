#pragma once

#include <QString>

namespace lsw::host::platform {

// Where the cross-built guest drivers are, on this machine.
//
// Four files per architecture, and the guest needs all of them to reach
// the host's GPU:
//
//   librvk_icd.dll     the render server's Vulkan driver
//   opengl32.dll       ... its OpenGL driver, which an application
//                      finds by the DLL search order rather than by a
//                      registry key, so it must be named exactly this
//   librvk_opencl.dll  ... its OpenCL driver
//   vulkan-1.dll       the Khronos loader. A VM with no GPU driver
//                      ships no loader at all, and without one nothing
//                      can find a Vulkan driver — the loader is what
//                      reads VK_DRIVER_FILES and loads what it names.
//
// The three drivers come out of the render-server submodule's half of
// the mingw cross-build; the loader out of the Vulkan-Loader that build
// fetches. Both live under ``build-agent`` (and ``build-agent32`` for
// the i686 set) — see scripts/build-agent.sh.
//
// As with the agent itself, an environment variable wins so a developer
// can point the wizard at a specific build. Every accessor returns an
// empty string when the file is not there, which the stager treats as
// "that driver is not available in this guest" rather than as an error.
class GuestDriverPaths {
public:
    GuestDriverPaths() = delete;

    [[nodiscard]] static QString vulkan_icd();
    [[nodiscard]] static QString opengl_icd();
    [[nodiscard]] static QString opencl_icd();
    [[nodiscard]] static QString vulkan_loader();

    // The same four, built for i686.
    //
    // A 32-bit process can only load a 32-bit driver, and every D3D8 /
    // D3D9 title is 32-bit — so a guest with only the x86-64 set can
    // run none of them.
    [[nodiscard]] static QString vulkan_icd_x86();
    [[nodiscard]] static QString opengl_icd_x86();
    [[nodiscard]] static QString opencl_icd_x86();
    [[nodiscard]] static QString vulkan_loader_x86();
};

}  // namespace lsw::host::platform
