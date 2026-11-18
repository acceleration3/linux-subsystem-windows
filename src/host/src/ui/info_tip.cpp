#include "lsw/host/ui/info_tip.hpp"

#include "lsw/host/ui/theme.hpp"

#include <algorithm>
#include <cmath>

#include <QAbstractTextDocumentLayout>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTextDocument>

namespace lsw::host::ui {

namespace {
constexpr int kSide = 15;
constexpr int kWidest = 360;
constexpr int kWidestLong = 480;
constexpr int kMostLines = 8;
constexpr int kNarrowest = 160;
constexpr int kGap = 6;
constexpr int kPadX = 11;
constexpr int kPadY = 8;
constexpr int kTextPixels = 13;
constexpr qreal kRadius = 6;

class Bubble : public QWidget {
public:
    Bubble() : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setObjectName(QStringLiteral("InfoBubble"));
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
        doc_.setDocumentMargin(0);
    }

    void show_below(QWidget* anchor, const QString& html) {
        if (parentWidget() != anchor->window()) {
            setParent(anchor->window(), windowFlags());
        }
        QFont text_font = anchor->font();
        text_font.setPixelSize(kTextPixels);
        doc_.setDefaultFont(text_font);
        doc_.setHtml(html);
        doc_.setTextWidth(balanced_width());
        const QSize text = doc_.size().toSize();
        setFixedSize(text.width() + 2 * kPadX, text.height() + 2 * kPadY);
        QPoint at = anchor->mapToGlobal(QPoint(anchor->width() / 2 - kSide, anchor->height() + kGap));
        if (const QScreen* screen = anchor->screen()) {
            const QRect room = screen->availableGeometry();
            at.setX(std::clamp(at.x(), room.left() + kGap, room.right() - width() - kGap));
            if (at.y() + height() > room.bottom()) {
                at.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - kGap);
            }
        }
        move(at);
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QString::fromLatin1(Theme::Color::kLine)), 1));
        painter.setBrush(QColor(QString::fromLatin1(Theme::Color::kRaised)));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
        painter.translate(kPadX, kPadY);
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, QColor(QString::fromLatin1(Theme::Color::kFg)));
        doc_.documentLayout()->draw(&painter, context);
    }

private:
    [[nodiscard]] qreal height_at(qreal width) {
        doc_.setTextWidth(width);
        return doc_.size().height();
    }

    [[nodiscard]] qreal balanced_width() {
        doc_.setTextWidth(-1);
        const qreal ideal = std::ceil(doc_.idealWidth()) + 1;
        if (ideal <= kWidest) {
            return ideal;
        }
        const int widest = kWidest;
        const qreal tallest = height_at(widest);
        if (tallest > kMostLines * QFontMetricsF(doc_.defaultFont()).lineSpacing()) {
            return kWidestLong;
        }
        int low = kNarrowest;
        int high = widest;
        while (low < high) {
            const int mid = (low + high) / 2;
            if (height_at(mid) > tallest) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        return high;
    }

    QTextDocument doc_;
};

Bubble& bubble() {
    static QPointer<Bubble> shared;
    if (shared.isNull()) {
        shared = new Bubble;
    }
    return *shared;
}
}

InfoTip::InfoTip(const QString& text, QWidget* parent) : QWidget(parent) {
    setFixedSize(kSide, kSide);
    setCursor(Qt::WhatsThisCursor);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(QStringLiteral("More about this"));
    set_text(text);
}

void InfoTip::set_text(const QString& text) {
    text_ = text;
    setAccessibleDescription(text_);
    show_if_placed();
    if (lit_ || hasFocus()) {
        show_tip();
    }
}

void InfoTip::append(const QString& text) {
    set_text(text_.isEmpty() ? text : text_ + QStringLiteral("<br><br>") + text);
}

void InfoTip::show_if_placed() {
    if (text_.isEmpty()) {
        hide();
    } else if (parentWidget() != nullptr && isHidden()) {
        show();
    }
}

bool InfoTip::event(QEvent* event) {
    if (event->type() == QEvent::ParentChange) {
        show_if_placed();
    }
    return QWidget::event(event);
}

QSize InfoTip::sizeHint() const {
    return {kSide, kSide};
}

void InfoTip::show_tip() {
    if (text_.isEmpty() || !isVisible()) {
        return;
    }
    bubble().show_below(this, text_);
}

void InfoTip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink(QString::fromLatin1(lit_ || hasFocus() ? Theme::Color::kFg : Theme::Color::kFgFaint));
    painter.setPen(QPen(ink, 1.2));
    painter.drawEllipse(QRectF(rect()).adjusted(0.6, 0.6, -0.6, -0.6));
    QFont glyph = font();
    glyph.setPixelSize(10);
    glyph.setBold(true);
    painter.setFont(glyph);
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("i"));
}

void InfoTip::enterEvent(QEnterEvent*) {
    lit_ = true;
    update();
    show_tip();
}

void InfoTip::leaveEvent(QEvent*) {
    lit_ = false;
    update();
    bubble().hide();
}

void InfoTip::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    update();
    show_tip();
}

void InfoTip::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    update();
    bubble().hide();
}

}
