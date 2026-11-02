#pragma once

#include <QString>

namespace lsw::host::platform {
class AccelVirtPaths {
public:
    AccelVirtPaths() = delete;

    [[nodiscard]] static QString qemu_binary();
    [[nodiscard]] static QString qemu_module_dir();
    [[nodiscard]] static QString qemu_data_dir();
    [[nodiscard]] static QString executor();
    [[nodiscard]] static QString d3d_lib_dir();
    [[nodiscard]] static QString driver_tree();
    [[nodiscard]] static QString vulkan_loader(bool x86);

private:
    struct QemuTree {
        QString dir;
        QString binary;
    };
    [[nodiscard]] static QemuTree qemu_tree();
    [[nodiscard]] static QString in_qemu_tree(const QString& name);
};

}
