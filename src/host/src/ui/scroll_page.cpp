#include "lsw/host/ui/scroll_page.hpp"

#include <QFrame>
#include <QEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace lsw::host::ui {

ScrollPage::ScrollPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    scroll_ = new QScrollArea;
    auto* scroll = scroll_;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    holder_ = new QWidget;
    auto* holder = holder_;
    holder->installEventFilter(this);
    column_ = new QVBoxLayout(holder);
    column_->setContentsMargins(14, 14, 14, 14);
    column_->setSpacing(12);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);
}

QSize ScrollPage::minimumSizeHint() const {
    const int bar = scroll_->verticalScrollBar()->sizeHint().width();
    return {holder_->minimumSizeHint().width() + bar, QWidget::minimumSizeHint().height()};
}

bool ScrollPage::eventFilter(QObject* watched, QEvent* event) {
    if (watched == holder_ && event->type() == QEvent::LayoutRequest) {
        updateGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

}
