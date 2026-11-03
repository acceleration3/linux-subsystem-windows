#include "lsw/host/ui/elided_label.hpp"

#include <QFontMetrics>
#include <QResizeEvent>

#include <algorithm>

namespace lsw::host::ui {

namespace {
constexpr int kLeastWidth = 24;
}

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    set_full_text(text);
}

void ElidedLabel::set_full_text(const QString& text) {
    full_ = text;
    setToolTip(text);
    elide();
    updateGeometry();
}

QSize ElidedLabel::sizeHint() const {
    const QSize base = QLabel::sizeHint();
    return {fontMetrics().horizontalAdvance(full_) + (base.width() - fontMetrics().horizontalAdvance(text())),
            base.height()};
}

QSize ElidedLabel::minimumSizeHint() const {
    return {kLeastWidth, QLabel::minimumSizeHint().height()};
}

void ElidedLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    elide();
}

void ElidedLabel::elide() {
    const int room = contentsRect().width();
    QLabel::setText(room > 0 ? fontMetrics().elidedText(full_, Qt::ElideRight, room) : full_);
}

}
