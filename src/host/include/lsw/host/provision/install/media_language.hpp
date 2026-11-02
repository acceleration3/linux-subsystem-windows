#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::install {
class MediaLanguage {
public:
    [[nodiscard]] static QStringList parse(const QString& lang_ini);

    [[nodiscard]] static QStringList of(const QString& iso_path, const QString& scratch_dir,
                                        QString* error_out);
};

}
