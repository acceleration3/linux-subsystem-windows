#include "lsw/host/platform/accel_virt_paths.hpp"

#include "lsw/host/platform/build_artifact.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace lsw::host::platform {

// Relative to the manager binary, which the root build puts at
// ${build}/src/host/lsw-manager; accel-virt's outputs are at ${build}.

QString AccelVirtPaths::in_qemu_build(const QString& name) {
    const QString named = qEnvironmentVariable("LSW_AV_QEMU_BUILD");
    if (!named.isEmpty()) {
        const QString candidate = QDir(named).filePath(name);
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return locate_build_artifact(QString{}, QStringLiteral("/../../accel-virt/qemu/") + name);
}

QString AccelVirtPaths::qemu_binary() {
    return in_qemu_build(QStringLiteral("qemu/qemu-system-x86_64"));
}

QString AccelVirtPaths::qemu_module_dir() {
    const QString binary = qemu_binary();
    return binary.isEmpty() ? QString{} : QFileInfo(binary).absolutePath();
}

QString AccelVirtPaths::executor() {
    return in_qemu_build(QStringLiteral("libav_executor.so"));
}

QString AccelVirtPaths::renderer() {
    return in_qemu_build(QStringLiteral("av-renderer"));
}

QString AccelVirtPaths::d3d_lib_dir() {
    return locate_build_artifact(QStringLiteral("LSW_AV_D3D_PATH"),
                                 QStringLiteral("/../../accel-virt/d3d/lib"));
}

QString AccelVirtPaths::driver_tree() {
    return locate_build_artifact(QStringLiteral("LSW_AV_DRIVERS"),
                                 QStringLiteral("/../../av-drivers"));
}

QString AccelVirtPaths::vulkan_loader(bool x86) {
    // The x86 override keeps its _X86 suffix: it names one specific
    // file, and a developer setting it is saying which build of that
    // architecture to use.
    return x86 ? locate_build_artifact(
                     QStringLiteral("LSW_VULKAN_LOADER_X86"),
                     QStringLiteral(
                         "/../../../build-agent32/_deps/vulkanloader-build/loader/vulkan-1.dll"))
               : locate_build_artifact(
                     QStringLiteral("LSW_VULKAN_LOADER"),
                     QStringLiteral(
                         "/../../../build-agent/_deps/vulkanloader-build/loader/vulkan-1.dll"));
}

}  // namespace lsw::host::platform
