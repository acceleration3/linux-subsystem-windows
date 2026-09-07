#include "lsw/host/manager/frame_tap.hpp"

#include <QMetaObject>
#include <QPainter>
#include <QWidget>

#include <utility>

namespace lsw::host::manager {

FrameTap& FrameTap::instance() {
    static FrameTap tap;
    return tap;
}

void FrameTap::attach(std::uint64_t hwnd, QWidget* mirror) {
    widgets_.insert(hwnd, QPointer<QWidget>(mirror));
}

void FrameTap::detach(std::uint64_t hwnd) {
    widgets_.remove(hwnd);
    const QMutexLocker lock(&mu_);
    entries_.remove(hwnd);
}

void FrameTap::record(std::uint64_t hwnd, Entry entry) {
    const QMutexLocker lock(&mu_);
    entries_.insert(hwnd, std::move(entry));
}

QList<std::uint64_t> FrameTap::hwnds() const {
    const QMutexLocker lock(&mu_);
    return entries_.keys();
}

std::optional<FrameTap::Entry> FrameTap::get(std::uint64_t hwnd) const {
    const QMutexLocker lock(&mu_);
    const auto it = entries_.constFind(hwnd);
    if (it == entries_.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

QImage FrameTap::grab_widget(std::uint64_t hwnd) {
    QImage out;
    // The whole lookup runs in the GUI thread, not just the render: a
    // widget pointer carried across threads could be deleted between
    // being read and being used.
    QMetaObject::invokeMethod(
        this,
        [this, hwnd, &out] {
            const auto it = widgets_.constFind(hwnd);
            if (it == widgets_.constEnd() || it->isNull()) {
                return;
            }
            QWidget* w = it->data();
            if (w->width() <= 0 || w->height() <= 0) {
                return;
            }
            // Into a transparent ARGB image rather than QWidget::grab,
            // whose pixmap flattens the alpha — and the alpha is the
            // point: a hole in a mirror is where nothing was painted.
            out = QImage(w->size(), QImage::Format_ARGB32_Premultiplied);
            out.fill(Qt::transparent);
            w->render(&out, QPoint(), QRegion(),
                      QWidget::DrawWindowBackground | QWidget::DrawChildren);
        },
        Qt::BlockingQueuedConnection);
    return out;
}

}  // namespace lsw::host::manager
