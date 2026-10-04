#pragma once

#include <QString>
#include <QtGlobal>

namespace lsw::host::ui {

// Sizes and times as the interface writes them.
//
// Binary units throughout, named as such: every size here is counted in
// powers of 1024, and a label that says GB over a 1024^3 divisor is a
// number that disagrees with the file manager beside it.
class Format {
public:
    Format() = delete;

    // ``bytes`` as a number of GiB, for sentences that name the unit
    // once: "3.1 of 8.0 GiB".
    [[nodiscard]] static double to_gib(qint64 bytes);

    // ``bytes`` in GiB, unit included: "12.5 GiB".
    [[nodiscard]] static QString gib(qint64 bytes, int decimals = 1);

    // A size in the largest binary unit it reaches: "512 B", "3.8 KiB",
    // "750.2 MiB", "4.1 GiB".
    [[nodiscard]] static QString bytes(quint64 bytes);

    // A configured size given in KiB: whole MiB when it is a whole
    // number of them, KiB otherwise. "16 MiB", "1536 KiB".
    [[nodiscard]] static QString kib(quint64 kib);

    // A transfer rate: "12.3 MiB/s".
    [[nodiscard]] static QString mib_per_second(double bytes_per_second);

    // ``seconds`` as m:ss, the minutes padded to ``minute_digits``; a
    // dash for a negative count, which is a time not known yet.
    [[nodiscard]] static QString clock(qint64 seconds, int minute_digits = 1);
};

}  // namespace lsw::host::ui
