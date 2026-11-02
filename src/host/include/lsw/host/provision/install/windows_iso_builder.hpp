#pragma once

#include <QString>

namespace lsw::host::provision::install {
class WindowsIsoBuilder {
public:
    WindowsIsoBuilder() = delete;

    [[nodiscard]] static bool build(const QString& source_iso, const QString& trimmed_wim,
                                    const QString& output_iso, const QString& scratch_dir,
                                    QString* error_out);

    static constexpr qint64 kIso9660FileLimit = 4LL * 1024 * 1024 * 1024;
};

}
