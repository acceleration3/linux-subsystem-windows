#pragma once

#include <QString>

namespace lsw::host::provision::install {

// Configures what goes onto the secondary ISO mounted alongside the
// Windows ISO. The autounattend.xml at the root drives the install
// (including all post-install bootstrap via its specialize-pass
// RunSynchronous block); the rest of the files live alongside it on the
// ISO root and are copied to ``C:\ProgramData\LSW\`` by the specialize
// block while the CDs are still mounted.
struct PayloadInputs {
    QString output_iso_path;     // where to write the resulting .iso
    QString autounattend_xml;    // already-rendered XML (string)
    QString lsw_msi_path;        // optional; empty = skip
    QString dxvk_archive_path;   // optional .tar.gz
    QString vkd3d_archive_path;  // optional .tar.gz
    QString agent_exe_path;      // optional; cross-built lsw-guest-agent.exe

    // Directory holding the IVSHMEM driver (ivshmem.inf/.cat/.sys).
    // Copied to ``\\ivshmem`` on the ISO so the autounattend can inject
    // it into the offline image — the shared-memory channel has to exist
    // on the FIRST boot, because it is what the agent talks over. A
    // driver installed later, over the channel it is supposed to
    // provide, is a bootstrap that only works if something else (TCP)
    // carries it.
    QString ivshmem_driver_dir;  // optional; empty = not staged

    // The guest Vulkan pair, staged into ``\vulkan`` on the ISO and
    // copied to ``C:\ProgramData\LSW\vulkan`` at install:
    //
    //   icd_dll_path        lsw_icd.dll — our driver
    //   vulkan_loader_path  vulkan-1.dll — the Khronos loader, without
    //                       which nothing can find the driver, because a
    //                       VM with no GPU has no loader of its own
    //
    // A manifest naming the DLL is written beside them and registered in
    // ``HKLM\SOFTWARE\Khronos\Vulkan\Drivers``; that registry value is
    // the entire discovery mechanism on Windows, the counterpart of the
    // .json file the Linux loader finds on disk.
    //
    // Both optional. Without them the guest installs and the agent runs;
    // only the GPU path is absent.
    QString icd_dll_path;
    QString vulkan_loader_path;
    QString icd_manifest_json;  // contents, not a path
};

}  // namespace lsw::host::provision::install
