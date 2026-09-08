#include "lsw/host/ui/toggle_switch.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QPainter>
#include <QPaintEvent>

namespace lsw::host::ui {

namespace {

// The design's pill: 34 by 19, a 13px knob with 3px of air either side.
constexpr int kWidth = 34;
constexpr int kHeight = 19;
constexpr int kKnob = 13;

QColor colour(const char* hex) {
    return QColor{QString::fromLatin1(hex)};
}

}  // namespace

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QSize ToggleSwitch::sizeHint() const {
    return {kWidth, kHeight};
}

void ToggleSwitch::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool on = isChecked();
    const QColor amber = colour(Theme::Color::kAmber);
    const QRectF track{0.5, (height() - kHeight) / 2.0 + 0.5, kWidth - 1.0, kHeight - 1.0};

    painter.setBrush(on ? QColor{255, 159, 69, 40} : colour("#20252f"));
    painter.setPen(QPen{on ? colour("#a8672a") : colour(Theme::Color::kLine), 1.0});
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);

    const qreal inset = (track.height() - kKnob) / 2.0;
    const qreal x = on ? track.right() - inset - kKnob : track.left() + inset;
    painter.setPen(Qt::NoPen);
    painter.setBrush(on ? amber : colour(Theme::Color::kFgFaint));
    painter.drawEllipse(QRectF{x, track.top() + inset, kKnob, kKnob});

    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{amber, 1.0});
        const QRectF ring = track.adjusted(-2, -2, 2, 2);
        painter.drawRoundedRect(ring, ring.height() / 2, ring.height() / 2);
    }
}

}  // namespace lsw::host::ui
