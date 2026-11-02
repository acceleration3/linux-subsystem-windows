#include "lsw/host/ui/vnc_view.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/ui/play_badge.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QEnterEvent>
#include <QEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <rfb/rfbclient.h>

#include <cstdlib>
#include <cstring>

namespace lsw::host::ui {

namespace {
constexpr int kClientDataSlot = 0;
constexpr int kRetryMs = 2000;

constexpr int kSettleMs = 500;
constexpr int kSettleCapMs = 3000;

constexpr const char* kPlaceholder = "#2b2f36";
const QColor kPausedDim{13, 15, 20, 115};
const QColor kHoverDim{13, 15, 20, 70};

QRect fit(QSize size, const QRect& area) {
    const QSize target = size.scaled(area.size(), Qt::KeepAspectRatio);
    return {area.x() + (area.width() - target.width()) / 2,
            area.y() + (area.height() - target.height()) / 2, target.width(), target.height()};
}
}

VncView::VncView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumHeight(360);
    setStyleSheet("background-color: black;");
    retry_timer_.setSingleShot(true);
    settle_timer_.setSingleShot(true);
    QObject::connect(&settle_timer_, &QTimer::timeout, this, [this] {
        if (streaming_ && pause_on_frame_.load()) {
            pause();
        }
    });
    QObject::connect(&retry_timer_, &QTimer::timeout, this, [this] {
        if (streaming_ && last_port_ > 0 && !last_host_.isEmpty()) {
            open_session(pause_on_frame_.load());
        }
    });
}

VncView::~VncView() {
    disconnect();
}

void VncView::set_preview(bool on) {
    const bool was_paused = is_paused();
    preview_ = on;
    state_changed(was_paused);
}

bool VncView::is_paused() const {
    return preview_ && last_port_ > 0 && (!streaming_ || pause_on_frame_.load());
}

void VncView::state_changed(bool was_paused) {
    const bool paused = is_paused();
    setCursor(preview_ && last_port_ > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
    if (paused != was_paused) {
        Q_EMIT paused_changed(paused);
    }
}

void VncView::connect_to(const QString& host, int port) {
    const bool was_paused = is_paused();
    last_host_ = host;
    last_port_ = port;
    frozen_ = QImage();
    open_session(preview_);
    state_changed(was_paused);
}

void VncView::clear() {
    const bool was_paused = is_paused();
    streaming_ = false;
    pause_on_frame_.store(false);
    retry_timer_.stop();
    settle_timer_.stop();
    disconnect();
    last_host_.clear();
    last_port_ = -1;
    frozen_ = QImage();
    {
        QMutexLocker lock(&image_mutex_);
        image_ = QImage();
        fb_width_ = 0;
        fb_height_ = 0;
        content_w_ = 0;
        content_h_ = 0;
    }
    state_changed(was_paused);
}

void VncView::play() {
    if (last_port_ <= 0 || last_host_.isEmpty()) {
        return;
    }
    const bool was_paused = is_paused();
    if (streaming_ && pause_on_frame_.load()) {
        pause_on_frame_.store(false);
        settle_timer_.stop();
    } else if (!streaming_) {
        open_session(false);
    }
    state_changed(was_paused);
}

void VncView::pause() {
    const bool was_paused = is_paused();
    streaming_ = false;
    pause_on_frame_.store(false);
    retry_timer_.stop();
    settle_timer_.stop();
    disconnect();
    freeze_frame();
    state_changed(was_paused);
}

void VncView::freeze_frame() {
    QImage frame;
    {
        QMutexLocker lock(&image_mutex_);
        if (image_.isNull() || content_w_ == 0 || content_h_ == 0) {
            return;
        }
        frame = image_.copy(0, 0, content_w_, content_h_);
    }
    frozen_ = frame.convertToFormat(QImage::Format_Grayscale8)
                  .convertToFormat(QImage::Format_RGB32);
    QPainter p{&frozen_};
    p.fillRect(frozen_.rect(), kPausedDim);
}

void VncView::enterEvent(QEnterEvent* e) {
    QWidget::enterEvent(e);
    hovered_ = true;
    if (preview_) {
        update();
    }
}

void VncView::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    hovered_ = false;
    if (preview_) {
        update();
    }
}

void VncView::mousePressEvent(QMouseEvent* e) {
    if (preview_ && e->button() == Qt::LeftButton) {
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void VncView::mouseReleaseEvent(QMouseEvent* e) {
    if (!preview_ || e->button() != Qt::LeftButton || last_port_ <= 0
        || !rect().contains(e->position().toPoint())) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    if (is_paused()) {
        play();
    } else {
        pause();
    }
}

void VncView::open_session(bool pause_on_frame) {
    disconnect();
    const auto& host = last_host_;
    const int port = last_port_;
    streaming_ = true;
    pause_on_frame_.store(pause_on_frame);
    settle_timer_.stop();
    first_frame_clock_.invalidate();
    ++session_;
    {
        QMutexLocker lock(&image_mutex_);
        image_ = QImage();
        fb_width_ = 0;
        fb_height_ = 0;
        content_w_ = 0;
        content_h_ = 0;
    }

    client_ = ::rfbGetClient(8, 3, 4);
    if (client_ == nullptr) {
        LSW_ERROR("rfbGetClient returned null");
        return;
    }
    ::rfbClientSetClientData(client_, reinterpret_cast<void*>(kClientDataSlot), this);
    client_->GotFrameBufferUpdate = &VncView::on_framebuffer_update_static;
    client_->FinishedFrameBufferUpdate = &VncView::on_finished_update_static;
    client_->MallocFrameBuffer = &VncView::on_malloc_framebuffer_static;
    client_->serverHost = ::strdup(host.toUtf8().constData());
    client_->serverPort = port;
    client_->appData.useRemoteCursor = FALSE;
    client_->canHandleNewFBSize = TRUE;

    int argc = 0;
    if (::rfbInitClient(client_, &argc, nullptr) == FALSE) {
        LSW_ERROR("rfbInitClient failed for vnc {}:{}", host.toStdString(), port);
        client_ = nullptr;
        if (streaming_) {
            retry_timer_.start(kRetryMs);
        }
        return;
    }
    retry_timer_.stop();
    stop_.store(false);
    worker_ = std::thread([this] { worker_loop(); });
}

void VncView::disconnect() {
    if (worker_.joinable()) {
        stop_.store(true);
        worker_.join();
    }
    if (client_ != nullptr) {
        ::rfbClientCleanup(client_);
        client_ = nullptr;
    }
}

void VncView::worker_loop() {
    const auto schedule_retry = [this] {
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (streaming_ && !retry_timer_.isActive()) {
                    retry_timer_.start(kRetryMs);
                }
            },
            Qt::QueuedConnection);
    };
    while (!stop_.load()) {
        const int n = ::WaitForMessage(client_, 100'000);
        if (n < 0) {
            LSW_WARN("vnc WaitForMessage failed; ending worker");
            schedule_retry();
            return;
        }
        if (n == 0) {
            continue;
        }
        if (::HandleRFBServerMessage(client_) == FALSE) {
            LSW_WARN("vnc HandleRFBServerMessage failed; ending worker");
            schedule_retry();
            return;
        }
    }
}

signed char VncView::on_malloc_framebuffer_static(_rfbClient* c) {
    auto* self = static_cast<VncView*>(
        ::rfbClientGetClientData(c, reinterpret_cast<void*>(kClientDataSlot)));
    if (self == nullptr) {
        return FALSE;
    }
    constexpr int kMaxW = 2560;
    constexpr int kMaxH = 1600;
    const int w = std::max(c->width, kMaxW);
    const int h = std::max(c->height, kMaxH);
    c->width = w;
    c->height = h;
    const auto line = std::string{"vnc MallocFrameBuffer server="} + std::to_string(c->width)
                      + "x" + std::to_string(c->height) + " (allocated " + std::to_string(w) + "x"
                      + std::to_string(h) + ", bpp=" + std::to_string(c->format.bitsPerPixel) + ")";
    LSW_INFO("{}", line);
    self->resize_buffer(w, h);
    c->frameBuffer = self->image_.bits();
    return TRUE;
}

void VncView::resize_buffer(int w, int h) {
    QMutexLocker lock(&image_mutex_);
    fb_width_ = w;
    fb_height_ = h;
    content_w_ = 0;
    content_h_ = 0;
    image_ = QImage(w, h, QImage::Format_RGBX8888);
    image_.fill(Qt::black);
    if (client_ != nullptr) {
        client_->frameBuffer = image_.bits();
    }
}

void VncView::on_framebuffer_update_static(_rfbClient* c, int x, int y, int w, int h) {
    auto* self = static_cast<VncView*>(
        ::rfbClientGetClientData(c, reinterpret_cast<void*>(kClientDataSlot)));
    if (self == nullptr) {
        return;
    }
    {
        QMutexLocker lock(&self->image_mutex_);
        self->content_w_ = std::max(self->content_w_, x + w);
        self->content_h_ = std::max(self->content_h_, y + h);
    }
    QMetaObject::invokeMethod(self, [self] { self->update(); }, Qt::QueuedConnection);
}

void VncView::on_finished_update_static(_rfbClient* c) {
    auto* self = static_cast<VncView*>(
        ::rfbClientGetClientData(c, reinterpret_cast<void*>(kClientDataSlot)));
    if (self == nullptr || !self->pause_on_frame_.load()) {
        return;
    }
    {
        QMutexLocker lock(&self->image_mutex_);
        if (self->content_w_ == 0 || self->content_h_ == 0) {
            return;
        }
    }
    const int session = self->session_;
    QMetaObject::invokeMethod(
        self, [self, session] { self->frame_arrived(session); }, Qt::QueuedConnection);
}

void VncView::frame_arrived(int session) {
    if (session != session_ || !streaming_ || !pause_on_frame_.load()) {
        return;
    }
    if (!first_frame_clock_.isValid()) {
        first_frame_clock_.start();
    }
    if (first_frame_clock_.elapsed() >= kSettleCapMs) {
        pause();
        return;
    }
    settle_timer_.start(kSettleMs);
}

void VncView::paintEvent(QPaintEvent* ) {
    QPainter p{this};
    const bool paused = is_paused();
    bool drew = false;
    if (!paused) {
        QMutexLocker lock(&image_mutex_);
        if (!image_.isNull() && content_w_ > 0 && content_h_ > 0) {
            const QRect src{0, 0, content_w_, content_h_};
            p.fillRect(rect(), Qt::black);
            p.drawImage(fit(src.size(), rect()), image_, src);
            drew = true;
        }
    }
    if (!drew && preview_ && !frozen_.isNull() && last_port_ > 0) {
        p.fillRect(rect(), Qt::black);
        p.drawImage(fit(frozen_.size(), rect()), frozen_);
        drew = true;
    }
    if (!drew) {
        p.fillRect(rect(), preview_ ? Theme::color(kPlaceholder) : QColor(Qt::black));
    }
    if (!preview_ || last_port_ <= 0) {
        return;
    }
    if (paused) {
        PlayBadge{true, hovered_}.paint(p, rect());
    } else if (hovered_) {
        p.fillRect(rect(), kHoverDim);
        PlayBadge{false, false}.paint(p, rect());
    }
}

}
