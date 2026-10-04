#include "lsw/host/config/display_modes.hpp"

#include <QRegularExpression>
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

std::optional<DisplayMode> DisplayModes::parse(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d{1,5})\\s*[xX×]\\s*(\\d{1,5})\\s*$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return DisplayMode{match.captured(1).toInt(), match.captured(2).toInt()};
}

qint64 DisplayModes::pixel_clock_hz(const DisplayMode& mode) {
    const qint64 total_width = qint64{mode.width} + kHorizontalBlank;
    const qint64 total_height = qint64{mode.height} + mode.height / kLinesPerBlankLine;
    return total_width * total_height * kRefreshHz;
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
    // The EDID field counts whole 10 kHz units, so the driver compares
    // the truncated count against 65535; this is the same test in Hz.
    const qint64 clock = pixel_clock_hz(mode);
    if (clock / 10'000 > kMaxPixelClockHz / 10'000) {
        return QStringLiteral("%1 needs a %2 MHz pixel clock at 60 Hz; the most is 655.35 MHz.")
            .arg(mode.label())
            .arg(static_cast<double>(clock) / 1e6, 0, 'f', 2);
    }
    return {};
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

}  // namespace lsw::host::config
