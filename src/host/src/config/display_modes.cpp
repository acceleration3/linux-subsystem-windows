#include "lsw/host/config/display_modes.hpp"

#include <QRegularExpression>
#include <algorithm>
#include <numeric>

namespace lsw::host::config {

QString DisplayMode::text() const {
    return QStringLiteral("%1x%2").arg(width).arg(height);
}

QString DisplayMode::label() const {
    return QStringLiteral("%1 × %2").arg(width).arg(height);
}

QString DisplayMode::aspect() const {
    if (width <= 0 || height <= 0) {
        return {};
    }
    const int divisor = std::gcd(width, height);
    return QStringLiteral("%1:%2").arg(width / divisor).arg(height / divisor);
}

const QVector<DisplayMode>& DisplayModes::common() {
    static const QVector<DisplayMode> sizes = {
        {1280, 720},  {1366, 768},  {1600, 900},  {1920, 1080}, {1920, 1200},
        {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 1600},
        {3840, 2160}, {5120, 1440}, {5120, 2160}, {5120, 2880}, {7680, 2160},
        {7680, 4320},
    };
    return sizes;
}

QVector<DisplayMode> DisplayModes::offerable(const QVector<DisplayMode>& monitors) {
    QVector<DisplayMode> out;
    for (const DisplayMode& mode : monitors) {
        if (refusal(mode).isEmpty() && !out.contains(mode)) {
            out.push_back(mode);
        }
    }
    for (const DisplayMode& mode : common()) {
        if (!out.contains(mode)) {
            out.push_back(mode);
        }
    }
    return out;
}

std::optional<DisplayMode> DisplayModes::parse(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d{1,5})\\s*[xX×]\\s*(\\d{1,5})\\s*$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return DisplayMode{match.captured(1).toInt(), match.captured(2).toInt()};
}

QString DisplayModes::refusal(const DisplayMode& mode) {
    if (mode.width < kMinEdge || mode.height < kMinEdge) {
        return QStringLiteral("%1 is under %2 on an edge, the smallest Windows accepts.")
            .arg(mode.label())
            .arg(kMinEdge);
    }
    if (mode.width > kMaxEdge || mode.height > kMaxEdge) {
        return QStringLiteral("%1 cannot be offered: %2 is the most an edge can be.")
            .arg(mode.label())
            .arg(kMaxEdge);
    }
    return {};
}

DisplayMode DisplayModes::clamped(const DisplayMode& mode) {
    return DisplayMode{std::clamp(mode.width, kMinEdge, kMaxEdge), std::clamp(mode.height, kMinEdge, kMaxEdge)};
}

QString DisplayModes::refusal_to_add(const QStringList& list, const DisplayMode& mode) {
    if (const QString why = refusal(mode); !why.isEmpty()) {
        return why;
    }
    const QStringList current = normalized(list);
    if (current.contains(mode.text())) {
        return QStringLiteral("%1 is already in the list.").arg(mode.label());
    }
    if (current.size() >= kMaxModes) {
        return QStringLiteral("The device offers at most %1 sizes.").arg(kMaxModes);
    }
    return {};
}

QStringList DisplayModes::normalized(const QStringList& list) {
    QStringList out;
    for (const QString& entry : list) {
        const auto mode = parse(entry);
        if (!mode || !refusal(*mode).isEmpty()) {
            continue;
        }
        const QString text = mode->text();
        if (!out.contains(text)) {
            out << text;
        }
    }
    return out;
}

QStringList DisplayModes::effective(const QStringList& list,
                                    const std::optional<DisplayMode>& output) {
    QStringList out = normalized(list);
    if (output && refusal(*output).isEmpty()) {
        out.removeAll(output->text());
        out.prepend(output->text());
    }
    while (out.size() > kMaxModes) {
        out.removeLast();
    }
    return out;
}

QString DisplayModes::property(const QStringList& list) {
    return list.join(QLatin1Char(','));
}

}
