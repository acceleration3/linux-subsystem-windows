#include "lsw/host/ui/content_height.hpp"

#include <QEvent>
#include <QLayout>
#include <QWidget>

#include <utility>

namespace lsw::host::ui {

ContentHeight::ContentHeight(QWidget* window, ContentSource content)
    : QObject(window), window_(window), content_(std::move(content)) {
    window_->installEventFilter(this);
}

void ContentHeight::keep(QWidget* window) {
    new ContentHeight(window, [window] { return window; });
}

void ContentHeight::keep(QWidget* window, ContentSource content) {
    new ContentHeight(window, std::move(content));
}

bool ContentHeight::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_ || watched == watched_content_) {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::LayoutRequest:
            schedule();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void ContentHeight::schedule() {
    if (pending_) {
        return;
    }
    pending_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            pending_ = false;
            apply();
        },
        Qt::QueuedConnection);
}

void ContentHeight::apply() {
    QWidget* content = content_();
    if (content == nullptr || content->layout() == nullptr) {
        return;
    }
    if (content != watched_content_) {
        if (watched_content_ != nullptr && watched_content_ != window_) {
            watched_content_->removeEventFilter(this);
        }
        watched_content_ = content;
        if (content != window_) {
            content->installEventFilter(this);
        }
    }
    const QLayout* layout = content->layout();
    const int content_needed = layout->hasHeightForWidth()
                                   ? layout->totalMinimumHeightForWidth(content->width())
                                   : layout->totalMinimumSize().height();
    const int chrome = content == window_ ? 0 : window_->height() - content->height();
    const int needed = chrome + content_needed;
    if (content_needed > 0 && chrome >= 0 && needed != window_->minimumHeight()) {
        window_->setMinimumHeight(needed);
    }
}

}
