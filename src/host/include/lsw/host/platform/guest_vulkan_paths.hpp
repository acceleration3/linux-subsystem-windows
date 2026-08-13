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

    // The ICD manifest the guest's loader reads. Generated rather than
    // shipped as a file, because it has to name the driver's path on the
    // GUEST filesystem, which the host only knows by convention.
    [[nodiscard]] static QString icd_manifest_json();
};

}  // namespace lsw::host::platform
