#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace lsw::host::config {
class DisplayRefresh {
public:
    static constexpr quint32 kLeastMillihz = 1000;
    static constexpr quint32 kMostMillihz = 1'000'000;
    static constexpr const char* kMonitor = "monitor";

    DisplayRefresh() = delete;

    [[nodiscard]] static std::optional<quint32> fixed(const QString& setting);

    [[nodiscard]] static std::optional<quint32> from_hertz(double hertz);

    [[nodiscard]] static std::optional<quint32> resolve(const QString& setting,
                                                        std::optional<quint32> monitor);

    [[nodiscard]] static QString text(quint32 millihz);

    [[nodiscard]] static QString label(std::optional<quint32> millihz);
};

}
