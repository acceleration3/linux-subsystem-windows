#pragma once

#include <QString>

namespace lsw::host::platform {

// Where accel-virt's pieces are on this machine: the guest's GPU driver
// tree, and the host half QEMU runs the guest's adapter with.
//
// None of it is installed. The root build leaves it in its own tree
// (cmake/LSWAccelVirt.cmake has the layout), beside the manager binary,
// and these follow the rule every other build artifact follows: an
// environment variable wins if it names something that exists,
// otherwise the build tree. Every accessor returns an empty string when
// neither exists, which a caller treats as "not available here".
//
//   LSW_AV_DRIVERS      the driver tree (the guest's C:\av)
//   LSW_AV_QEMU_BUILD   the directory holding qemu/, libav_executor.so
//                       and av-renderer, as accel-virt's build-qemu does
//   LSW_AV_D3D_PATH     DXVK's and vkd3d-proton's native libraries
//
// The QEMU override names a directory rather than each file because
// the three only work together: a device built by one QEMU tree loads
// the executor ABI that tree was built against.
class AccelVirtPaths {
public:
    AccelVirtPaths() = delete;

    // The patched QEMU. The distribution's cannot load the device: its
    // compiled-in module table has no entry for it.
    [[nodiscard]] static QString qemu_binary();
    // Where that QEMU finds its modules (QEMU_MODULE_DIR): the directory
    // the binary is in.
    [[nodiscard]] static QString qemu_module_dir();
    // The device's executor= library, which runs guest commands on the
    // host GPU.
    [[nodiscard]] static QString executor();
    // The device's renderer=, the process the executor runs in when the
    // host GPU stack is isolated from QEMU.
    [[nodiscard]] static QString renderer();
    // AV_D3D_PATH. Without it the host serves no Direct3D and the
    // guest's desktop crash-loops.
    [[nodiscard]] static QString d3d_lib_dir();
    // The tree the payload ISO carries and the guest copies to C:\av:
    // provision.ps1, the signed-at-install package, and the OpenCL
    // loaders under loaders/x64 and loaders/x86.
    [[nodiscard]] static QString driver_tree();
    // The Khronos vulkan-1.dll for the guest, x64 or (``x86``) the
    // 32-bit one SysWOW64 needs. Windows ships neither, and accel-virt
    // does not build them: they come out of our own mingw cross builds,
    // build-agent and build-agent32 (scripts/build-agent.sh), with
    // $LSW_VULKAN_LOADER and $LSW_VULKAN_LOADER_X86 as the overrides.
    [[nodiscard]] static QString vulkan_loader(bool x86);

private:
    // ``$LSW_AV_QEMU_BUILD/<name>`` if that exists, else the build
    // tree's accel-virt/qemu/<name>. Empty when neither does.
    [[nodiscard]] static QString in_qemu_build(const QString& name);
};

}  // namespace lsw::host::platform
