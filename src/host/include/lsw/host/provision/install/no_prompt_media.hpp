#pragma once

#include <QString>

namespace lsw::host::provision::install {
class NoPromptMedia {
public:
    NoPromptMedia() = delete;

    [[nodiscard]] static bool make(const QString& source_iso, const QString& overlay_path,
                                   QString* error_out);
};

}
