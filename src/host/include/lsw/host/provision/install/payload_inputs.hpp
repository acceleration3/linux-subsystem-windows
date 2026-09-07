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

    // The guest drivers, staged into ``\\rvk`` (and ``\\rvk32``) on the
    // ISO and copied to ``C:\\ProgramData\\LSW\\rvk`` at install, with
    // the Khronos Vulkan loader into ``\\vulkan``:
    //
    //   vulkan_icd_path   librvk_icd.dll  — the render server's Vulkan
    //                     driver, staged as rvk_icd.dll
    //   opengl_icd_path   opengl32.dll    — its OpenGL driver, which an
    //                     application finds by the DLL search order
    //   opencl_icd_path   librvk_opencl.dll, staged as rvk_opencl.dll
    //   vulkan_loader_path  vulkan-1.dll  — the Khronos loader, without
    //                     which nothing can find a Vulkan driver,
    //                     because a VM with no GPU has none of its own
    //
    // No manifest and no registry value: the agent writes an ICD
    // manifest beside each application it launches and names it in that
    // launch's VK_DRIVER_FILES. Which driver an application uses is a
    // per-application decision, and a machine-wide registry key is the
    // wrong scope for one.
    //
    // All optional. Without them the guest installs and the agent runs;
    // only the GPU path is absent.
    QString vulkan_icd_path;
    QString opengl_icd_path;
    QString opencl_icd_path;
    QString vulkan_loader_path;
    // The i686 set, for 32-bit applications. Optional: a guest without
    // it installs fine and runs 64-bit applications, and only a D3D8 /
    // D3D9 title notices.
    QString vulkan_icd32_path;
    QString opengl_icd32_path;
    QString opencl_icd32_path;
    QString vulkan32_loader_path;
};

}  // namespace lsw::host::provision::install
