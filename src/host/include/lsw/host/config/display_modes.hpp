#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace lsw::host::config {
struct DisplayMode {
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(const DisplayMode& other) const noexcept {
        return width == other.width && height == other.height;
    }

    [[nodiscard]] QString text() const;
    [[nodiscard]] QString label() const;
    [[nodiscard]] QString aspect() const;
};

class DisplayModes {
public:
    static constexpr int kMinEdge = 100;
    static constexpr int kMaxEdge = 4095;
    static constexpr int kMaxModes = 32;

    DisplayModes() = delete;

    [[nodiscard]] static std::optional<DisplayMode> parse(const QString& text);

    [[nodiscard]] static QString refusal(const DisplayMode& mode);

    [[nodiscard]] static QString refusal_to_add(const QStringList& list, const DisplayMode& mode);

    [[nodiscard]] static QStringList normalized(const QStringList& list);

    [[nodiscard]] static QStringList effective(const QStringList& list,
                                               const std::optional<DisplayMode>& output);

    [[nodiscard]] static QString property(const QStringList& list);
};

}
