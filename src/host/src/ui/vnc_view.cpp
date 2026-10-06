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
constexpr int kClientDataSlot = 0;  // arbitrary, unique to us
constexpr int kRetryMs = 2000;      // reconnect cadence while desired

// A paused start's frame. QEMU answers a new client first with its own
// copy of the screen, which it stops refreshing while nobody is
// connected, so that first complete update is stale (black, on a guest
// that has been up a while); the real picture follows on its next
// refresh tick, some 100ms later. So the frame is taken once updates
// have been quiet this long after the first one, or, for a guest that
// never stops drawing, at the cap.
constexpr int kSettleMs = 500;
constexpr int kSettleCapMs = 3000;

// The preview's paused look, as the design draws it: the surface before
// any frame exists, how far a paused frame is dimmed once desaturated,
// and the dim over a live frame while the pointer is on it.
constexpr const char* kPlaceholder = "#2b2f36";
const QColor kPausedDim{13, 15, 20, 115};
const QColor kHoverDim{13, 15, 20, 70};

// ``size`` letterboxed into ``area``, centred.
QRect fit(QSize size, const QRect& area) {
    const QSize target = size.scaled(area.size(), Qt::KeepAspectRatio);
    return {area.x() + (area.width() - target.width()) / 2,
            area.y() + (area.height() - target.height()) / 2, target.width(), target.height()};
}
}  // namespace

VncView::VncView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);  // we always cover the full rect
    // Only a minimum height — no minimum width, so the surface can be
    // squeezed narrow (e.g. in a split panel) and just letterboxes.
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
    // Something to click only where there is something to play.
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
    // A new target's picture is not the old one's.
    frozen_ = QImage();
    open_session(preview_);
    state_changed(was_paused);
}

void VncView::clear() {
    const bool was_paused = is_paused();
    streaming_ = false;  // set first so no in-flight retry reschedules
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
        // Still waiting on a paused start's frame: the connection is
        // already there, so it simply stays.
        pause_on_frame_.store(false);
        settle_timer_.stop();
    } else if (!streaming_) {
        open_session(false);
    }
    state_changed(was_paused);
}

void VncView::pause() {
    const bool was_paused = is_paused();
    streaming_ = false;  // set first so no in-flight retry reschedules
    pause_on_frame_.store(false);
    retry_timer_.stop();
    settle_timer_.stop();
    disconnect();  // the worker is gone, so the frame holds still
    freeze_frame();
    state_changed(was_paused);
}

void VncView::freeze_frame() {
    QImage frame;
    {
        QMutexLocker lock(&image_mutex_);
        if (image_.isNull() || content_w_ == 0 || content_h_ == 0) {
            return;  // nothing new arrived: keep the frame we had
        }
        frame = image_.copy(0, 0, content_w_, content_h_);
    }
    // Desaturated, then dimmed, once: paintEvent only scales it.
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
        e->accept();  // so the release comes here too
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
    // The worker reads this; it is only written here, with no worker
    // running, before the next one starts.
    ++session_;
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
    client_->FinishedFrameBufferUpdate = &VncView::on_finished_update_static;
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
        // as long as a connection is wanted.
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

void VncView::on_finished_update_static(_rfbClient* c) {
    auto* self = static_cast<VncView*>(
        ::rfbClientGetClientData(c, reinterpret_cast<void*>(kClientDataSlot)));
    if (self == nullptr || !self->pause_on_frame_.load()) {
        return;
    }
    {
        // A whole update message is done. Until one has drawn something
        // there is no frame yet: an update that only resized the
        // framebuffer drew nothing (resize_buffer zeroes the extent).
        QMutexLocker lock(&self->image_mutex_);
        if (self->content_w_ == 0 || self->content_h_ == 0) {
            return;
        }
    }
    // The pause joins this thread, so the settling is timed on the GUI
    // thread. A click to play may get there first, and a newer session
    // may have replaced this one: both are checked there.
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
    settle_timer_.start(kSettleMs);  // restarted by every update
}

void VncView::paintEvent(QPaintEvent* /*e*/) {
    QPainter p{this};
    const bool paused = is_paused();
    bool drew = false;
    if (!paused) {
        // Drawn straight from the framebuffer, cropped to the painted
        // region (the buffer is over-allocated), with no copy of it.
        QMutexLocker lock(&image_mutex_);
        if (!image_.isNull() && content_w_ > 0 && content_h_ > 0) {
            const QRect src{0, 0, content_w_, content_h_};
            p.fillRect(rect(), Qt::black);
            p.drawImage(fit(src.size(), rect()), image_, src);
            drew = true;
        }
    }
    if (!drew && preview_ && !frozen_.isNull() && last_port_ > 0) {
        // Paused, or playing with the new stream not drawn yet: the
        // last frame, greyed.
        p.fillRect(rect(), Qt::black);
        p.drawImage(fit(frozen_.size(), rect()), frozen_);
        drew = true;
    }
    if (!drew) {
        // The wizard's view stays black until the guest draws; the
        // preview is grey, so an empty one never reads as a dark screen.
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

}  // namespace lsw::host::ui
