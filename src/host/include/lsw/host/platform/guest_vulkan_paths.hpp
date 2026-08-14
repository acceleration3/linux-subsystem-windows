#pragma once

#include <QString>

namespace lsw::host::platform {

// Where the cross-built guest Vulkan pieces are, on this machine.
//
// Two files, and the guest needs both:
//
//   lsw_icd.dll    our driver — the same sources as the Linux
//                  liblsw_icd.so, differing only in which SharedRegion
//                  maps the pages
//   vulkan-1.dll   the Khronos loader. A VM with no GPU driver ships no
//                  loader at all, and without one nothing can find the
//                  ICD: the loader is what reads
//                  HKLM\SOFTWARE\Khronos\Vulkan\Drivers and loads what
//                  it names.
//
// Both come out of ``scripts/build-agent.sh``. As with the agent
// itself, an environment variable wins so a developer can point the
// wizard at a specific build.
class GuestVulkanPaths {
public:
    GuestVulkanPaths() = delete;

    // ``$LSW_ICD_DLL`` if it names an existing file, else the build-tree
    // default. Empty when neither exists.
    [[nodiscard]] static QString icd_dll();

    // ``$LSW_VULKAN_LOADER`` if it names an existing file, else the
    // build-tree default. Empty when neither exists.
    [[nodiscard]] static QString vulkan_loader();

    // The same two files, built for i686.
    //
    // A 32-bit process can only load a 32-bit driver, and every D3D8 /
    // D3D9 title is 32-bit — so a guest with only the x86-64 pair can
    // run none of them. They come out of the second cross-build (see
    // scripts/build-agent.sh) and are staged beside the 64-bit pair,
    // registered under the WOW6432Node view of the driver key because
    // that is the one a 32-bit process reads.
    [[nodiscard]] static QString icd_dll_x86();
    [[nodiscard]] static QString vulkan_loader_x86();

    // The ICD manifest the guest's loader reads. Generated rather than
    // shipped as a file, because it has to name the driver's path on the
    // GUEST filesystem, which the host only knows by convention.
    [[nodiscard]] static QString icd_manifest_json();
};

}  // namespace lsw::host::platform
