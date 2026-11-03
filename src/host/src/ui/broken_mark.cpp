#include "lsw/host/ui/broken_mark.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPainterPath>

namespace lsw::host::ui {

namespace {
constexpr qreal kDimmed = 0.38;
constexpr qreal kTilt = -8.0;
constexpr int kTextPx = 13;
constexpr qreal kLetterSpacing = 3.0;
constexpr int kPadX = 14;
constexpr int kPadY = 5;
constexpr qreal kBorder = 1.5;
constexpr qreal kRadius = 4.0;
constexpr int kBackdropAlpha = 230;
}

BrokenMark::BrokenMark(QWidget* panel) : QWidget(panel), panel_(panel) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setGeometry(panel_->rect());
    panel_->installEventFilter(this);
    raise();
    show();
}

void BrokenMark::apply(QWidget* panel) {
    panel->setEnabled(false);
    for (QWidget* child : panel->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        auto* dim = new QGraphicsOpacityEffect(child);
        dim->setOpacity(kDimmed);
        child->setGraphicsEffect(dim);
    }
    new BrokenMark(panel);
}

bool BrokenMark::eventFilter(QObject* watched, QEvent* event) {
    if (watched == panel_ && event->type() == QEvent::Resize) {
        setGeometry(panel_->rect());
        raise();
    }
    return QWidget::eventFilter(watched, event);
}

void BrokenMark::paintEvent(QPaintEvent*) {
    QFont font = Theme::mono_font(kTextPx);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, kLetterSpacing);
    const QString text = QStringLiteral("NOT WORKING");
    const QFontMetrics metrics(font);
    const QRectF label(-(metrics.horizontalAdvance(text) / 2.0) - kPadX, -(metrics.height() / 2.0) - kPadY,
                       metrics.horizontalAdvance(text) + (2.0 * kPadX), metrics.height() + (2.0 * kPadY));

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect().center());
    painter.rotate(kTilt);
    const QColor bad = Theme::color(Theme::Color::kBad);
    QColor backdrop = Theme::color(Theme::Color::kBadSurface);
    backdrop.setAlpha(kBackdropAlpha);
    QPainterPath shape;
    shape.addRoundedRect(label, kRadius, kRadius);
    painter.fillPath(shape, backdrop);
    painter.setPen(QPen(bad, kBorder));
    painter.drawPath(shape);
    painter.setFont(font);
    painter.drawText(label, Qt::AlignCenter, text);
}

}
