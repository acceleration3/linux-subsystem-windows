#pragma once

#include <QString>
#include <QVector>
#include <optional>

namespace lsw::host::provision::install {
struct ElToritoEntry {
    QString image_path;
    int platform_id = 0;
    short load_sectors = 0;
};

struct ElToritoBoot {
    QString catalog_path;
    QVector<ElToritoEntry> entries;
};

struct IsoWriteOptions {
    QString volume_id;
    int iso_level = 3;
    bool joliet = true;
    bool rock_ridge = true;
};

[[nodiscard]] bool write_iso_from_tree(const QString& tree_dir, const QString& output_iso,
                                       const IsoWriteOptions& options,
                                       const std::optional<ElToritoBoot>& boot, QString* error_out);

}
