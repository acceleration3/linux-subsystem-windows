#pragma once

#include <QString>

namespace lsw::host::provision::install {

// Configures what goes onto the secondary ISO mounted alongside the
// Windows ISO. The autounattend.xml at the root drives the install
// (including all post-install bootstrap via its specialize-pass
// RunSynchronous block); the rest of the files live alongside it on the
// ISO root and are copied to ``C:\ProgramData\LSW\`` (and the driver
// package to ``C:\av``) by the specialize block while the CDs are still
// mounted.
struct PayloadInputs {
    QString output_iso_path;   // where to write the resulting .iso
    QString autounattend_xml;  // already-rendered XML (string)
    QString lsw_msi_path;      // optional; empty = skip
    QString agent_exe_path;    // optional; cross-built lsw-guest-agent.exe
    // optional; remotewin's guest half, started beside a launched
    // application and dialling back out to the mirror process the
    // manager runs for it
    QString mirror_exe_path;

    // The accel-virt driver package, as ``cmake --install`` lays it
    // out (``AccelVirtPaths::driver_tree``): provision.ps1, package\ with
    // avgpu.sys/.inf, the user-mode drivers for both architectures and
    // package.sha256, and loaders\{x64,x86}\OpenCL.dll.
    // Staged whole, recursively, as ``\av`` on the ISO; the specialize
    // pass copies it to ``C:\av`` and SetupComplete runs its
    // provision.ps1 to sign and install the driver.
    //
    // Empty stages nothing. Whether a guest may be built without it is
    // the caller's decision, not the ISO's: PayloadIsoStager refuses,
    // because a guest whose only display is a device with no driver
    // has no desktop once the install-time VGA adapter is gone.
    QString av_driver_tree;
    // ``\av\adapters.txt``: this host's GPU names, which provision.ps1
    // names each GPU's INF model with (AdapterNames::render). Written in
    // place of any adapters.txt the tree carries, which names the GPUs
    // of whatever machine built the package. Empty writes none, and the
    // guest names each model as the INF's static one.
    QString av_adapter_names;
    // The Khronos Vulkan loaders, which the driver package does not
    // carry: ``vulkan-1.dll`` for x64 and for x86, staged as
    // ``\av\loaders\x64\vulkan-1.dll`` and
    // ``\av\loaders\x86\vulkan-1.dll`` beside the package's own
    // OpenCL loaders. Optional each: the guest copies a loader only
    // where Windows has none, and recent builds ship one.
    QString vulkan_loader_x64_path;
    QString vulkan_loader_x86_path;
};

}  // namespace lsw::host::provision::install
