#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace lsw::host::config {

// The guest's refresh rate: the one number the accel-virt GPU device's
// ``refresh`` property takes, in millihertz, and Windows is told for every
// mode. Pure functions, so the setting, the domain definition, the
// settings page and the tests agree on what a rate is and how it reads.
//
// The setting (``display/refresh``) is "monitor", the default, which
// follows the rate of the monitor the manager is on, or a rate in
// millihertz.
class DisplayRefresh {
public:
    // The device's range: 1 Hz to 1 kHz (DisplayTimingRequest).
    static constexpr quint32 kLeastMillihz = 1000;
    static constexpr quint32 kMostMillihz = 1'000'000;
    // What the setting holds for "same as the monitor".
    static constexpr const char* kMonitor = "monitor";

    DisplayRefresh() = delete;

    // The rate ``setting`` stores; nullopt for "monitor" and for anything
    // that is no rate the device takes, which reads as "monitor" too.
    [[nodiscard]] static std::optional<quint32> fixed(const QString& setting);

    // A rate in hertz (QScreen::refreshRate) as the device's millihertz;
    // nullopt for what is no rate (zero, negative, past the range).
    [[nodiscard]] static std::optional<quint32> from_hertz(double hertz);

    // What the device is given for ``setting``: its own rate, or the
    // monitor's when the setting follows it and the caller knows it.
    // nullopt when neither is known.
    [[nodiscard]] static std::optional<quint32> resolve(const QString& setting,
                                                        std::optional<quint32> monitor);

    // The setting's text for a rate: the millihertz as a number.
    [[nodiscard]] static QString text(quint32 millihz);

    // "360 Hz", "239.76 Hz": whole hertz without decimals, otherwise up
    // to three, trailing zeros dropped. "-" for no rate.
    [[nodiscard]] static QString label(std::optional<quint32> millihz);
};

}  // namespace lsw::host::config
