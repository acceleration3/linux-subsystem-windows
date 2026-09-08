#include "lsw/host/ui/lsw_style.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOption>
#include <QWizard>
#include <QWizardPage>

namespace lsw::host::ui {

namespace {

QColor colour(const char* hex) {
    return QColor{QString::fromLatin1(hex)};
}

// A tick, drawn rather than typed: two strokes across the box, inset so
// the joint has somewhere to sit.
void draw_check(QPainter* painter, const QRect& box, const QColor& ink) {
    QPen pen{ink};
    pen.setWidthF(2.1);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    const qreal x = box.x();
    const qreal y = box.y();
    const qreal w = box.width();
    const qreal h = box.height();
    QPainterPath path;
    path.moveTo(x + w * 0.24, y + h * 0.52);
    path.lineTo(x + w * 0.43, y + h * 0.72);
    path.lineTo(x + w * 0.77, y + h * 0.29);
    painter->drawPath(path);
}

}  // namespace

LswStyle::LswStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

int LswStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                          const QWidget* widget) const {
    // QWizard lays its page, its side widget and its button row out with
    // the style's own margins, and there is no API to set them. It asks
    // twice: once about itself for the outer margin, and once about the
    // current page for the inner one that surrounds both the page and
    // the rail. Both have to answer zero, or the design's rail and page
    // float in a border of the window's ground.
    if (widget != nullptr
        && (qobject_cast<const QWizard*>(widget) != nullptr
            || qobject_cast<const QWizardPage*>(widget) != nullptr)) {
        switch (metric) {
        case PM_LayoutLeftMargin:
        case PM_LayoutRightMargin:
        case PM_LayoutTopMargin:
        case PM_LayoutBottomMargin:
        case PM_LayoutHorizontalSpacing:
        case PM_LayoutVerticalSpacing:
            return 0;
        default:
            break;
        }
    }
    switch (metric) {
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return 16;
    default:
        break;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

void LswStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                             QPainter* painter, const QWidget* widget) const {
    const bool on = (option->state & State_On) != 0;
    const bool enabled = (option->state & State_Enabled) != 0;
    const bool hovered = (option->state & State_MouseOver) != 0;

    const QColor amber = colour(Theme::Color::kAmber);
    const QColor line = colour(Theme::Color::kLine);
    const QColor faint = colour(Theme::Color::kFgFaint);
    const QColor base = colour("#0f131a");
    const QColor ink = colour(Theme::Color::kInk);

    switch (element) {
    case PE_IndicatorCheckBox: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setBrush(on ? amber : base);
        painter->setPen(QPen{on ? amber : (hovered && enabled ? amber : faint), 1.0});
        painter->drawRoundedRect(box, 3, 3);
        if (on) {
            draw_check(painter, option->rect, ink);
        }
        painter->restore();
        return;
    }
    case PE_IndicatorRadioButton: {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setBrush(base);
        painter->setPen(QPen{on ? amber : (hovered && enabled ? amber : faint), 1.0});
        painter->drawEllipse(box);
        if (on) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(amber);
            painter->drawEllipse(QRectF(option->rect).adjusted(4, 4, -4, -4));
        }
        painter->restore();
        return;
    }
    case PE_IndicatorArrowDown: {
        // The combo box caret. A solid triangle, because the border
        // trick a stylesheet has to use renders as a dash at this size.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QPointF centre = QRectF(option->rect).center();
        const qreal half = 3.5;
        QPainterPath path;
        path.moveTo(centre.x() - half, centre.y() - half / 2);
        path.lineTo(centre.x() + half, centre.y() - half / 2);
        path.lineTo(centre.x(), centre.y() + half);
        path.closeSubpath();
        painter->setPen(Qt::NoPen);
        painter->setBrush(enabled ? colour(Theme::Color::kFgDim) : faint);
        painter->drawPath(path);
        painter->restore();
        return;
    }
    default:
        break;
    }
    (void)line;
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

}  // namespace lsw::host::ui
