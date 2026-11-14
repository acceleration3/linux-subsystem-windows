#include "lsw/host/platform/accel_virt_paths.hpp"

#include "lsw/host/platform/build_artifact.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace lsw::host::platform {
AccelVirtPaths::QemuTree AccelVirtPaths::qemu_tree() {
    const QString exe_dir = QCoreApplication::applicationDirPath();
    const auto build_tree = [](const QString& dir) -> QemuTree {
        return {dir, QDir(dir).filePath(QStringLiteral("qemu/qemu-system-x86_64")),
                QDir(dir).filePath(QStringLiteral("qemu"))};
    };
    const QString installed = QDir::cleanPath(exe_dir + QStringLiteral("/../lib/lsw/accel-virt/qemu"));
    const QString named = qEnvironmentVariable("LSW_AV_QEMU_BUILD");
    const QemuTree candidates[] = {
        named.isEmpty() ? QemuTree{} : build_tree(named),
        build_tree(QDir::cleanPath(exe_dir + QStringLiteral("/../../accel-virt/qemu"))),
        {installed, QDir(exe_dir).filePath(QStringLiteral("lsw-qemu-system-x86_64")), installed},
    };
    for (const QemuTree& tree : candidates) {
        if (!tree.binary.isEmpty() && QFile::exists(tree.binary)) {
            return tree;
        }
    }
    return {};
}

QString AccelVirtPaths::in_qemu_tree(const QString& name) {
    const QemuTree tree = qemu_tree();
    if (tree.dir.isEmpty()) {
        return {};
    }
    const QString path = QDir(tree.dir).filePath(name);
    return QFile::exists(path) ? path : QString{};
}

QString AccelVirtPaths::qemu_binary() {
    return qemu_tree().binary;
}

QString AccelVirtPaths::qemu_module_dir() {
    return qemu_tree().module_dir;
}

QString AccelVirtPaths::qemu_data_dir() {
    const QString modules = qemu_module_dir();
    if (modules.isEmpty()) {
        return {};
    }
    const QString dir = QDir(modules).filePath(QStringLiteral("share"));
    return QFileInfo(dir).isDir() ? dir : QString{};
}

QString AccelVirtPaths::executor() {
    return in_qemu_tree(QStringLiteral("libav_executor.so"));
}

QString AccelVirtPaths::d3d_lib_dir() {
    return locate_build_artifact(QStringLiteral("LSW_AV_D3D_PATH"),
                                 QStringLiteral("/../../accel-virt/d3d/lib"),
                                 QStringLiteral("/../lib/lsw/accel-virt/d3d"));
}

QString AccelVirtPaths::driver_tree() {
    return locate_build_artifact(QStringLiteral("LSW_AV_DRIVERS"),
                                 QStringLiteral("/../../av-drivers"),
                                 QStringLiteral("/../share/lsw/guest/av-drivers"));
}

QString AccelVirtPaths::vulkan_loader(bool x86) {
    return x86 ? locate_build_artifact(
                     QStringLiteral("LSW_VULKAN_LOADER_X86"),
                     QStringLiteral(
                         "/../../../build-agent32/_deps/vulkanloader-build/loader/vulkan-1.dll"),
                     QStringLiteral("/../share/lsw/guest/loaders/x86/vulkan-1.dll"))
               : locate_build_artifact(
                     QStringLiteral("LSW_VULKAN_LOADER"),
                     QStringLiteral(
                         "/../../../build-agent/_deps/vulkanloader-build/loader/vulkan-1.dll"),
                     QStringLiteral("/../share/lsw/guest/loaders/x64/vulkan-1.dll"));
}

}
