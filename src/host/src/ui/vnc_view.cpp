#include "lsw/host/ui/vnc_view.hpp"

#include "lsw/base/log.hpp"

#include <QEnterEvent>
#include <QEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <rfb/rfbclient.h>

#include <cstdlib>
#include <cstring>

namespace lsw::host::ui {

namespace {
constexpr int kClientDataSlot = 0;  // arbitrary, unique to us
constexpr int kRetryMs = 2000;      // reconnect cadence while desired
}

VncView::VncView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);  // we always cover the full rect
    // Only a minimum height — no minimum width, so the surface can be
    // squeezed narrow (e.g. in a split panel) and just letterboxes.
    setMinimumHeight(360);
    setStyleSheet("background-color: black;");
    retry_timer_.setSingleShot(true);
    QObject::connect(&retry_timer_, &QTimer::timeout, this, [this] {
        if (streaming_ && last_port_ > 0 && !last_host_.isEmpty()) {
            connect_to(last_host_, last_port_);
        }
    });
}

void VncView::set_controls_visible(bool on) {
    controls_visible_ = on;
    if (on && toggle_btn_ == nullptr) {
        toggle_btn_ = new QPushButton(this);
        toggle_btn_->setCursor(Qt::PointingHandCursor);
        toggle_btn_->setStyleSheet(
            "QPushButton{color:white;background:rgba(20,20,20,180);"
            "border:1px solid rgba(255,255,255,140);border-radius:6px;"
            "padding:8px 20px;font-weight:600;}"
            "QPushButton:hover{background:rgba(40,40,40,210);}");
        QObject::connect(toggle_btn_, &QPushButton::clicked, this, [this] {
            if (streaming_) {
                stop();
            } else {
                start();
            }
        });
        refresh_button();
        reposition_button();
    }
    if (toggle_btn_ != nullptr) {
        toggle_btn_->setVisible(on && hovered_);
    }
    update();
}

void VncView::refresh_button() {
    if (toggle_btn_ == nullptr) {
        return;
    }
    toggle_btn_->setText(streaming_ ? QStringLiteral("Stop preview")
                                    : QStringLiteral("Start preview"));
    toggle_btn_->adjustSize();
}

void VncView::reposition_button() {
    if (toggle_btn_ == nullptr) {
        return;
    }
    const QSize bs = toggle_btn_->sizeHint();
    toggle_btn_->setGeometry((width() - bs.width()) / 2,
                             (height() - bs.height()) / 2, bs.width(),
                             bs.height());
}

void VncView::start() {
    if (last_port_ > 0 && !last_host_.isEmpty()) {
        connect_to(last_host_, last_port_);  // sets streaming_ + refreshes
    }
}

void VncView::stop() {
    streaming_ = false;  // set first so no in-flight retry reschedules
    retry_timer_.stop();
    disconnect();
    refresh_button();
    update();
}

void VncView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    reposition_button();
}

void VncView::enterEvent(QEnterEvent* e) {
    QWidget::enterEvent(e);
    hovered_ = true;
    if (controls_visible_ && toggle_btn_ != nullptr) {
        refresh_button();
        reposition_button();
        toggle_btn_->setVisible(true);
        toggle_btn_->raise();
    }
    if (controls_visible_) {
        update();
    }
}

void VncView::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    hovered_ = false;
    if (toggle_btn_ != nullptr) {
        toggle_btn_->setVisible(false);
    }
    if (controls_visible_) {
        update();
    }
}

VncView::~VncView() {
    disconnect();
}

void VncView::connect_to(const QString& host, int port) {
    disconnect();
    last_host_ = host;
    last_port_ = port;
    streaming_ = true;
    refresh_button();
    {
        QMutexLocker lock(&image_mutex_);
        image_ = QImage();
        fb_width_ = 0;
        fb_height_ = 0;
        content_w_ = 0;
        content_h_ = 0;
    }

    // 8 bits per sample, 3 samples per pixel, 4 bytes per pixel.
    // libvncclient's default for this on little-endian sets
    // redShift=0, greenShift=8, blueShift=16 → memory bytes are R,G,B,A.
    // That matches QImage::Format_RGBX8888 (which is byte-order-explicit
    // R,G,B,X regardless of endianness). Format_RGB32 would NOT match
    // here: it's a packed uint32 0xAARRGGBB, which on LE flips to
    // B,G,R,X in memory — red and blue end up swapped, blue paints
    // brown, etc.
    client_ = ::rfbGetClient(8, 3, 4);
    if (client_ == nullptr) {
        LSW_ERROR("rfbGetClient returned null");
        return;
    }
    ::rfbClientSetClientData(client_, reinterpret_cast<void*>(kClientDataSlot), this);
    client_->GotFrameBufferUpdate = &VncView::on_framebuffer_update_static;
    client_->MallocFrameBuffer = &VncView::on_malloc_framebuffer_static;
    client_->serverHost = ::strdup(host.toUtf8().constData());
    client_->serverPort = port;
    // Don't advertise remote-cursor support: without a GotCursorShape
    // callback installed, the cursor pseudo-rects libvncclient receives
    // trip its "Rect too large" bounds check and kill the session.
    // Letting the server draw the cursor into the framebuffer is fine
    // for an install-progress view.
    client_->appData.useRemoteCursor = FALSE;
    // Advertise both resize encodings. QEMU sends NewFBSize when the
    // guest changes display mode (text → VGA → SVGA → driver-set
    // resolution) and our framebuffer must follow — without this, the
    // first post-resize rect overruns the still-tiny initial buffer
    // and libvncclient kills the session with "Rect too large".
    // ExtDesktopSize is announced unconditionally; NewFBSize only if
    // canHandleNewFBSize is set.
    client_->canHandleNewFBSize = TRUE;

    // rfbInitClient takes argc/argv for option parsing — we don't need
    // any so feed it an empty argv.
    int argc = 0;
    if (::rfbInitClient(client_, &argc, nullptr) == FALSE) {
        // ``rfbInitClient`` already freed ``client_`` on failure.
        LSW_ERROR("rfbInitClient failed for vnc {}:{}", host.toStdString(), port);
        client_ = nullptr;
        // The VM may not be up yet, or QEMU dropped us — keep trying
        // as long as the preview is meant to be streaming.
        if (streaming_) {
            retry_timer_.start(kRetryMs);
        }
        return;
    }
    retry_timer_.stop();  // connected; cancel any pending retry
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
    // Marshalled onto the GUI thread when the RFB loop ends because
    // the server went away (not because we asked it to stop) — keep
    // the preview alive by scheduling a reconnect.
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
        const int n = ::WaitForMessage(client_, 100'000);  // 100ms
        if (n < 0) {
            LSW_WARN("vnc WaitForMessage failed; ending worker");
            schedule_retry();
            return;
        }
        if (n == 0) {
            continue;  // poll timed out, loop and check stop_
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
    // QEMU's VNC implementation reports its CURRENT framebuffer size in
    // ServerInit and then changes geometry without emitting either a
    // NewFBSize or ExtDesktopSize encoded rect — so libvncclient's
    // internal bounds check rejects the first post-resize update with
    // "Rect too large" and the session dies. Pre-allocate a buffer
    // large enough for any reasonable guest mode and lie about
    // client->width/height accordingly. The server clamps update
    // requests to its real framebuffer, so over-allocation doesn't
    // cause spurious traffic; we just have black borders around the
    // active region until the guest hits the higher resolution.
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
    content_w_ = 0;  // unknown until first update arrives
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
    // Marshal a repaint onto the GUI thread; we can't paint from here.
    QMetaObject::invokeMethod(self, [self] { self->update(); }, Qt::QueuedConnection);
}

void VncView::paintEvent(QPaintEvent* /*e*/) {
    QPainter p{this};
    p.fillRect(rect(), Qt::black);
    QImage snapshot;
    {
        QMutexLocker lock(&image_mutex_);
        if (image_.isNull() || content_w_ == 0 || content_h_ == 0) {
            return;
        }
        // Crop to the actual painted region (full image is over-allocated).
        snapshot = image_.copy(0, 0, content_w_, content_h_);
    }
    const QSize target = snapshot.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect dst{(width() - target.width()) / 2, (height() - target.height()) / 2,
                    target.width(), target.height()};
    p.drawImage(dst, snapshot);

    // Preview hover affordance: dim the image toward grey so the
    // centered Start/Stop button (a child widget painted on top of
    // this) reads clearly. Only when controls are enabled (never in
    // the wizard) and the pointer is over the surface.
    if (controls_visible_ && hovered_) {
        p.fillRect(rect(), QColor(128, 128, 128, 110));
    }
}

}  // namespace lsw::host::ui
