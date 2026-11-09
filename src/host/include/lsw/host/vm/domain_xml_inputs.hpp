#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::vm {
struct DomainShare {
    QString tag;
    QString host_path;
    bool read_only = false;
    QString drive;

    static constexpr const char* kAliasPrefix = "ua-share-";
};

struct DomainXmlInputs {
    QVector<DomainShare> shares;

    bool install_display = false;
    QString host_gpu_uuid;
    QStringList render_only_gpu_uuids;
    bool hybrid_graphics = false;
    int host_window_mib = 8192;
    int vram_reserve_mib = 0;
    QString display_modes;
    quint32 display_refresh_millihz = 0;
    QString avgpu_log;
    QString export_socket;
    QString input_socket;

    QString qemu_binary;
    QString qemu_module_dir;
    QString qemu_data_dir;
    QString executor;
    QString d3d_lib_dir;
    QString shader_cache_dir;
    QString dxvk_log_level;
    QString vkd3d_debug;
};

}
