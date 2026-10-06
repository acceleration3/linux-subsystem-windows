#include "lsw/host/ui/play_badge.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QRect>

namespace lsw::host::ui {

namespace {
constexpr qreal kDisc = 56.0;
constexpr qreal kBarW = 6.0;
constexpr qreal kBarH = 20.0;
constexpr qreal kBarGap = 6.0;
constexpr qreal kCaptionGap = 10.0;
const QColor kDiscFill{13, 15, 20, 191};
constexpr int kRingAlpha = 90;
}  // namespace

PlayBadge::PlayBadge(bool paused, bool hot) noexcept : paused_(paused), hot_(hot) {}

void PlayBadge::paint(QPainter& p, const QRect& area) const {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont font = Theme::mono_font(10);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    const QFontMetricsF metrics{font};
    const qreal block = kDisc + kCaptionGap + metrics.height();
    const QRectF disc{area.x() + (area.width() - kDisc) / 2.0,
                      area.y() + (area.height() - block) / 2.0, kDisc, kDisc};

    const QColor ink = hot_ ? Theme::color(Theme::Color::kAmber) : Theme::color(Theme::Color::kFg);
    QColor ring = Theme::color(Theme::Color::kFg);
    ring.setAlpha(kRingAlpha);
    p.setPen(QPen{hot_ ? ink : ring, 1.0});
    p.setBrush(kDiscFill);
    p.drawEllipse(disc.adjusted(0.5, 0.5, -0.5, -0.5));

    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    const qreal top = disc.center().y() - kBarH / 2.0;
    const qreal left = disc.center().x() - kBarGap / 2.0 - kBarW;
    p.drawRoundedRect(QRectF{left, top, kBarW, kBarH}, 1.5, 1.5);
    p.drawRoundedRect(QRectF{left + kBarW + kBarGap, top, kBarW, kBarH}, 1.5, 1.5);

    p.setFont(font);
    p.setPen(Theme::color(Theme::Color::kFgDim));
    const QRectF caption{static_cast<qreal>(area.x()), disc.bottom() + kCaptionGap,
                         static_cast<qreal>(area.width()), metrics.height()};
    p.drawText(caption, Qt::AlignHCenter | Qt::AlignTop,
               paused_ ? QStringLiteral("click to play") : QStringLiteral("click to pause"));
    p.restore();
}

}  // namespace lsw::host::ui
