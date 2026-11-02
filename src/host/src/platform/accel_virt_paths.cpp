#include "lsw/host/platform/accel_virt_paths.hpp"

#include "lsw/host/platform/build_artifact.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <utility>

namespace lsw::host::platform {
AccelVirtPaths::QemuTree AccelVirtPaths::qemu_tree() {
    const QString build_binary = QStringLiteral("qemu/qemu-system-x86_64");
    const QString exe_dir = QCoreApplication::applicationDirPath();
    const QString named = qEnvironmentVariable("LSW_AV_QEMU_BUILD");
    const std::pair<QString, QString> candidates[] = {
        {named, build_binary},
        {QDir::cleanPath(exe_dir + QStringLiteral("/../../accel-virt/qemu")), build_binary},
        {QDir::cleanPath(exe_dir + QStringLiteral("/../lib/lsw/accel-virt/qemu")),
         QStringLiteral("qemu-system-x86_64")},
    };
    for (const auto& [dir, binary] : candidates) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString path = QDir(dir).filePath(binary);
        if (QFile::exists(path)) {
            return {dir, path};
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
    const QString binary = qemu_binary();
    return binary.isEmpty() ? QString{} : QFileInfo(binary).absolutePath();
}

QString AccelVirtPaths::qemu_data_dir() {
    const QString binary = qemu_binary();
    if (binary.isEmpty()) {
        return {};
    }
    const QString dir = QFileInfo(binary).absoluteDir().filePath(QStringLiteral("share"));
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
