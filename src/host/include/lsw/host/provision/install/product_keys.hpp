#pragma once

#include <QString>

namespace lsw::host::provision::install {
class ProductKeys {
public:
    ProductKeys() = delete;

    [[nodiscard]] static QString for_edition(const QString& edition_id);

    [[nodiscard]] static QString fallback();
};

}
