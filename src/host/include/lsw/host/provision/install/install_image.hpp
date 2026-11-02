#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {
struct StagedInstallImage {
    QString path;

    QStringList extra_parts;

    [[nodiscard]] bool is_split() const noexcept { return !extra_parts.isEmpty(); }

    [[nodiscard]] QStringList all_files() const {
        QStringList files{path};
        files.append(extra_parts);
        return files;
    }
};

[[nodiscard]] bool stage_install_image(const QString& iso_path, const QString& dir,
                                       StagedInstallImage* out, QString* error_out);

}
