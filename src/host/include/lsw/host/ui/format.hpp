#pragma once

#include <QString>
#include <QtGlobal>

namespace lsw::host::ui {
class Format {
public:
    Format() = delete;

    [[nodiscard]] static double to_gib(qint64 bytes);

    [[nodiscard]] static QString gib(qint64 bytes, int decimals = 1);

    [[nodiscard]] static QString bytes(quint64 bytes);

    [[nodiscard]] static QString kib(quint64 kib);

    [[nodiscard]] static QString mib_per_second(double bytes_per_second);

    [[nodiscard]] static QString clock(qint64 seconds, int minute_digits = 1);
};

}
