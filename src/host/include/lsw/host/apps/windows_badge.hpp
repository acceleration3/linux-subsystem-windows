#pragma once

#include <QByteArray>

namespace lsw::host::apps {
class WindowsBadge {
public:
    [[nodiscard]] static QByteArray applied(const QByteArray& png);
    [[nodiscard]] static QByteArray raw_mark();

    static constexpr int kSize = 256;
};

}
