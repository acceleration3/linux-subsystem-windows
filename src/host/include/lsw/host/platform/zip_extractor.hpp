#pragma once

#include <QString>

namespace lsw::host::platform {

class ZipExtractor {
public:
    ZipExtractor() = delete;

    [[nodiscard]] static bool extract(const QString& zip_path, const QString& out_dir,
                                      QString& error);
};

}
