#pragma once

#include <QString>

namespace lsw::host::platform {

// Where one architecture's cross-built guest drivers are, on this
// machine.
//
// Four files, and the guest needs all of them to reach the host's GPU:
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
// An instance, not a set of static lookups, because "which
// architecture" is the only thing that varies and it varies for every
// one of them. As eight static accessors the answer was encoded in the
// method name, so a caller wanting both sets named all eight and a
// fifth driver would have meant two more.
//
// The three drivers come out of the render-server submodule's half of
// the mingw cross-build; the loader out of the Vulkan-Loader that build
// fetches. Both live under ``build-agent``, or ``build-agent32`` for the
// i686 set — see scripts/build-agent.sh.
//
// Every accessor returns an empty string when the file is not there,
// which the stager treats as "that driver is not available in this
// guest" rather than as an error.
class GuestDriverPaths {
public:
    // The x86-64 set, and the i686 set a 32-bit process needs — every
    // D3D8 and D3D9 title is one, so a guest with only the first can run
    // none of them.
    [[nodiscard]] static GuestDriverPaths x86_64();
    [[nodiscard]] static GuestDriverPaths i686();

    [[nodiscard]] QString vulkan_icd() const;
    [[nodiscard]] QString opengl_icd() const;
    [[nodiscard]] QString opencl_icd() const;
    [[nodiscard]] QString vulkan_loader() const;

private:
    // ``build_tree`` is relative to the manager binary; ``env_prefix``
    // names the developer overrides, which are ``<prefix>_VULKAN_ICD``
    // and friends.
    GuestDriverPaths(QString build_tree, QString env_prefix);

    // ``$<env_prefix>_<name>`` if it names an existing file, else the
    // build tree. Empty when neither exists.
    [[nodiscard]] QString locate(const char* name, const char* relative) const;

    QString build_tree_;
    QString env_prefix_;
};

}  // namespace lsw::host::platform
