#include "lsw/host/manager/desktop_preview.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/ui/play_badge.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QEnterEvent>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QShowEvent>
#include <QTimer>
#include <nlohmann/json.hpp>

#ifdef LSW_HAVE_REMOTEWIN
#include "host/link/guest_link.hpp"
#include "host/link/pixels_view.hpp"
#include "lsw/host/manager/qt_event_loop.hpp"
#include "proto/feature.hpp"
#include "proto/window_id.hpp"
#endif

#include <cstddef>
#include <cstring>
#include <memory>

namespace lsw::host::manager {

namespace {
constexpr const char* kPlaceholder = "#2b2f36";
const QColor kPausedDim{13, 15, 20, 115};
const QColor kHoverDim{13, 15, 20, 70};
constexpr int kTagInset = 9;
constexpr int kSettleMs = 300;
constexpr int kStartTimeoutMs = 60000;

const QString kPaused = QStringLiteral("WGC, paused");
const QString kNoAgent = QStringLiteral("WGC, waiting for guest agent");

QRect fit(QSize size, const QRect& area) {
    const QSize target = size.scaled(area.size(), Qt::KeepAspectRatio);
    return {area.x() + (area.width() - target.width()) / 2,
            area.y() + (area.height() - target.height()) / 2, target.width(), target.height()};
}
}

class DesktopPreview::Session : public QObject {
public:
    explicit Session(QObject* parent) : QObject(parent) {}

#ifdef LSW_HAVE_REMOTEWIN
    QtEventLoop loop;
    rwin::host::GuestLink link{loop};
#endif
    QImage picture;
    QTimer deadline;
    QTimer settle;
    bool framed = false;
};

class DesktopPreview::Surface : public QWidget {
public:
    explicit Surface(DesktopPreview& preview) : QWidget(&preview), preview_(preview) {}

protected:
    void paintEvent(QPaintEvent* ) override {
        QPainter p{this};
        preview_.paint_surface(p, rect());
    }
    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        preview_.hovered_ = true;
        update();
    }
    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        preview_.hovered_ = false;
        update();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            preview_.toggle();
        }
    }

private:
    DesktopPreview& preview_;
};

DesktopPreview::DesktopPreview(link::AgentRpcClient& rpc, QWidget* parent)
    : QWidget(parent), rpc_(rpc), surface_(new Surface(*this)), tag_(new QLabel(this)) {
    setObjectName(QStringLiteral("DesktopPreview"));
    tag_->setObjectName(QStringLiteral("ScreenTag"));
    tag_->setAttribute(Qt::WA_TransparentForMouseEvents);
    note(kNoAgent);
}

DesktopPreview::~DesktopPreview() {
    stop();
}

void DesktopPreview::note(const QString& status) {
    status_ = status;
    tag_->setText(status);
    tag_->adjustSize();
    surface_->setCursor(available_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
    surface_->update();
}

void DesktopPreview::clear(const QString& why) {
    available_ = false;
    failed_ = false;
    frozen_ = {};
    set_paused(true);
    stop();
    note(why);
}

void DesktopPreview::set_available(bool available) {
    if (available_ == available) return;
    available_ = available;
    failed_ = false;
    stop();
    if (!available_) frozen_ = {};
    note(!available_ ? kNoAgent : paused_ ? kPaused : QStringLiteral("WGC, connecting"));
    start();
}

void DesktopPreview::toggle() {
    if (!available_) return;
    if (failed_) {
        failed_ = false;
        note(kPaused);
        set_paused(false);
        start();
        return;
    }
    set_paused(!paused_);
}

void DesktopPreview::set_paused(bool paused) {
    if (paused_ == paused) return;
    if (paused) freeze();
    paused_ = paused;
    if (paused_) {
        stop();
        if (available_ && !failed_) note(kPaused);
    } else {
        failed_ = false;
        if (session_ == nullptr) note(QStringLiteral("WGC, connecting"));
        start();
    }
    surface_->update();
    Q_EMIT paused_changed(paused_);
}

void DesktopPreview::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    start();
}

void DesktopPreview::hideEvent(QHideEvent* event) {
    if (!starting_) stop();
    QWidget::hideEvent(event);
}

void DesktopPreview::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    lay_out();
}

void DesktopPreview::lay_out() {
    surface_->setGeometry(rect());
    surface_->raise();
    tag_->move(kTagInset, kTagInset);
    tag_->raise();
}

void DesktopPreview::paint_surface(QPainter& p, const QRect& area) const {
    const bool live = session_ != nullptr && !paused_;
    const QImage& shown = live ? session_->picture : frozen_;
    if (shown.isNull()) {
        p.fillRect(area, ui::Theme::color(kPlaceholder));
    } else {
        p.fillRect(area, Qt::black);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(fit(shown.size(), area), shown);
    }
    if (!available_) return;
    if (paused_) {
        ui::PlayBadge{true, hovered_}.paint(p, area);
    } else if (hovered_) {
        p.fillRect(area, kHoverDim);
        ui::PlayBadge{false, false}.paint(p, area);
    }
}

void DesktopPreview::freeze() {
    if (session_ == nullptr || !session_->framed || session_->picture.isNull()) return;
    frozen_ = session_->picture.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_RGB32);
    QPainter p{&frozen_};
    p.fillRect(QRect(QPoint(0, 0), frozen_.size()), kPausedDim);
}

void DesktopPreview::stop() {
    if (session_ == nullptr) return;
    session_->deadline.stop();
    session_->settle.stop();
    session_->deleteLater();
    session_ = nullptr;
    surface_->update();
}

void DesktopPreview::fail(const QString& reason) {
    failed_ = true;
    freeze();
    paused_ = true;
    stop();
    note(QStringLiteral("WGC preview: %1").arg(reason));
    Q_EMIT paused_changed(paused_);
}

void DesktopPreview::start() {
    const bool wanted = !paused_ || frozen_.isNull();
    if (!available_ || !isVisible() || !wanted || failed_ || starting_ || session_ != nullptr) {
        return;
    }
    const QScopedValueRollback<bool> starting(starting_, true);
#ifndef LSW_HAVE_REMOTEWIN
    fail(QStringLiteral("remotewin was not built (LSW_BUILD_REMOTEWIN)"));
#else
    auto* session = new Session(this);
    session_ = session;
    rwin::host::GuestLink& link = session->link;
    if (!link.listen(0, true)) {
        fail(QStringLiteral("cannot listen for the guest"));
        return;
    }
    lay_out();
    if (!isVisible()) {
        stop();
        return;
    }
    link.connected.connect([this, session] {
        if (session_ != session) return;
        if (!session->link.features().has(rwin::Feature::kMonitor)) {
            fail(QStringLiteral("the guest's remotewin cannot capture its monitor"));
            return;
        }
        session->link.watch_monitor();
    });
    link.mismatched.connect([this, session](const std::string& reason) {
        if (session_ == session) fail(QString::fromStdString(reason));
    });
    link.disconnected.connect([this, session] {
        if (session_ == session) fail(QStringLiteral("guest capture disconnected"));
    });
    link.surface_lost.connect([this, session](rwin::WindowId window) {
        if (session_ == session && window == rwin::kMonitorWindow) fail(QStringLiteral("guest monitor capture stopped"));
    });
    const auto framed = [this, session] {
        if (session_ != session) return;
        if (!session->framed) {
            session->framed = true;
            session->deadline.stop();
            session->settle.start(kSettleMs);
        }
        const QString route = QStringLiteral("WGC, pixels");
        if (!paused_ && status_ != route) note(route);
        surface_->update();
    };
    link.frame_arrived.connect([session, framed](rwin::WindowId window, const rwin::host::PixelsView& pixels) {
        if (window != rwin::kMonitorWindow) return;
        const QImage frame(pixels.bgra, pixels.width, pixels.height, pixels.stride,
                           QImage::Format_ARGB32_Premultiplied);
        session->picture = frame.convertToFormat(QImage::Format_RGB32);
        framed();
    });
    link.frame_patched.connect([session](rwin::WindowId window, rwin::host::Size picture, rwin::host::Point at,
                                         const rwin::host::PixelsView& pixels) {
        if (window != rwin::kMonitorWindow || session->picture.size() != QSize(picture.width, picture.height)) return;
        for (int row = 0; row < pixels.height; ++row) {
            std::memcpy(session->picture.scanLine(at.y + row) + static_cast<std::ptrdiff_t>(at.x) * 4,
                        pixels.bgra + static_cast<std::ptrdiff_t>(row) * pixels.stride,
                        static_cast<std::size_t>(pixels.width) * 4);
        }
    });
    link.frame_patches_done.connect([framed](rwin::WindowId window) {
        if (window == rwin::kMonitorWindow) framed();
    });
    session->settle.setSingleShot(true);
    connect(&session->settle, &QTimer::timeout, this, [this, session] {
        if (session_ != session || !paused_) return;
        freeze();
        stop();
    });
    session->deadline.setSingleShot(true);
    connect(&session->deadline, &QTimer::timeout, this, [this, session] {
        if (session_ == session) fail(QStringLiteral("no desktop frame within 60 seconds"));
    });
    session->deadline.start(kStartTimeoutMs);
    rpc_.request_async(linkproto::cmd::kStartWindowMirror,
                       {{"port", link.port()}, {"desktop", true}}, session,
                       [this, session](const link::AgentReply& reply) {
                           if (session_ == session && !reply.ok) {
                               fail(QString::fromStdString(reply.error));
                           }
                       });
#endif
}

}
