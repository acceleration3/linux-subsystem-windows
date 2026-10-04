#pragma once

#include <QString>

namespace lsw::host::platform {

// Where accel-virt's pieces are on this machine: the guest's GPU driver
// tree, and the host half QEMU runs the guest's adapter with.
//
// These follow the rule every other build artifact follows
// (locate_build_artifact): an environment variable wins if it names
// something that exists, then the root build's tree beside the manager
// binary (cmake/LSWAccelVirt.cmake has that layout), then the installed
// tree beside it. Every accessor returns an empty string when none
// exists, which a caller treats as "not available here".
//
//   LSW_AV_DRIVERS      the driver tree (the guest's C:\av)
//   LSW_AV_QEMU_BUILD   the directory holding qemu/ and
//                       libav_executor.so, as accel-virt's build-qemu does
//   LSW_AV_D3D_PATH     DXVK's and vkd3d-proton's native libraries
//
// The QEMU override names a directory rather than each file because
// the two only work together: a device built by one QEMU tree loads
// the executor ABI that tree was built against. For the same reason the
// two are looked up as a set — the first of the override, the build
// tree and the installed tree that has the QEMU binary supplies both,
// so a stale build tree never pairs its QEMU with an installed
// executor.
//
// Installed, relative to <prefix>:
//
//   lib/lsw/accel-virt/qemu/          qemu-system-x86_64, its modules,
//                                     libav_executor.so
//   lib/lsw/accel-virt/qemu/share/    QEMU's firmware and ROMs (-L)
//   lib/lsw/accel-virt/d3d/           DXVK and vkd3d-proton
//   share/lsw/guest/av-drivers/       the driver tree
//   share/lsw/guest/loaders/x64|x86/  vulkan-1.dll
class AccelVirtPaths {
public:
    AccelVirtPaths() = delete;

    // The patched QEMU. The distribution's cannot load the device: its
    // compiled-in module table has no entry for it.
    [[nodiscard]] static QString qemu_binary();
    // Where that QEMU finds its modules (QEMU_MODULE_DIR): the directory
    // the binary is in.
    [[nodiscard]] static QString qemu_module_dir();
    // Where that QEMU finds its firmware and option ROMs (bios-256k.bin,
    // efi-virtio.rom, vgabios-stdvga.bin, kvmvapic.bin, the keymaps): the
    // ``share`` directory beside an installed binary, which the domain
    // passes as ``-L``. Empty for a build tree, where QEMU finds its own
    // qemu-bundle/ next to itself and needs no help.
    [[nodiscard]] static QString qemu_data_dir();
    // The device's executor= library, which runs guest commands on the
    // host GPU, inside QEMU.
    [[nodiscard]] static QString executor();
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
    // The directory the QEMU binary and the executor are looked up in,
    // given where the binary itself sits within it: ``$LSW_AV_QEMU_BUILD``
    // or the build tree's accel-virt/qemu (the binary under qemu/), else
    // the installed lib/lsw/accel-virt/qemu (the binary at its top).
    // Empty paths when none has the binary.
    struct QemuTree {
        QString dir;
        QString binary;
    };
    [[nodiscard]] static QemuTree qemu_tree();
    // ``name`` within qemu_tree()'s directory, if it exists there.
    [[nodiscard]] static QString in_qemu_tree(const QString& name);
};

}  // namespace lsw::host::platform
