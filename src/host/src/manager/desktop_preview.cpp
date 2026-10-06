#include "lsw/host/manager/desktop_preview.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/ui/play_badge.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/vm/export_socket.hpp"
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
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/monitor_session.hpp"
#include "proto/message_id.hpp"
#endif

#include <memory>

namespace lsw::host::manager {

namespace {
// The design's paused look: the surface before any frame exists, how far
// a paused frame is dimmed once desaturated, and the dim over a live one
// while the pointer is on it.
constexpr const char* kPlaceholder = "#2b2f36";
const QColor kPausedDim{13, 15, 20, 115};
const QColor kHoverDim{13, 15, 20, 70};
// Where the tag sits on the picture.
constexpr int kTagInset = 9;
// A paused start's frame is taken this long after the first one arrives,
// so the GPU path has drawn it.
constexpr int kSettleMs = 300;
constexpr int kStartTimeoutMs = 60000;

const QString kPaused = QStringLiteral("WGC, paused");
const QString kNoAgent = QStringLiteral("WGC, waiting for guest agent");

// ``size`` letterboxed into ``area``, centred.
QRect fit(QSize size, const QRect& area) {
    const QSize target = size.scaled(area.size(), Qt::KeepAspectRatio);
    return {area.x() + (area.width() - target.width()) / 2,
            area.y() + (area.height() - target.height()) / 2, target.width(), target.height()};
}
}  // namespace

class DesktopPreview::Session : public QObject {
public:
    explicit Session(QObject* parent) : QObject(parent) {}

    // The view releases its fenced slot leases before the feed and link go.
#ifdef LSW_HAVE_REMOTEWIN
    rwin::host::GuestLink link;
    rwin::host::SharedFrameFeed frames{link};
    std::unique_ptr<rwin::host::MonitorSession> monitor;
#endif
    QTimer deadline;
    QTimer settle;
    bool framed = false;
};

// Children draw over their parent, and the live view is a child: what is
// drawn over it is drawn by a sibling above it.
class DesktopPreview::Surface : public QWidget {
public:
    explicit Surface(DesktopPreview& preview) : QWidget(&preview), preview_(preview) {}

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
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
        // A paused start still taking its frame goes on, live.
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
    // Adding the first QOpenGLWidget can recreate the top-level native
    // window. Its temporary hide/show is not a request to restart capture.
    if (!starting_) stop();
    QWidget::hideEvent(event);
}

void DesktopPreview::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    lay_out();
}

void DesktopPreview::lay_out() {
#ifdef LSW_HAVE_REMOTEWIN
    if (session_ != nullptr && session_->monitor) session_->monitor->view().setGeometry(rect());
#endif
    surface_->setGeometry(rect());
    surface_->raise();
    tag_->move(kTagInset, kTagInset);
    tag_->raise();
}

void DesktopPreview::paint_surface(QPainter& p, const QRect& area) const {
    const bool live = session_ != nullptr && !paused_;
    if (!live) {
        if (frozen_.isNull()) {
            p.fillRect(area, ui::Theme::color(kPlaceholder));
        } else {
            p.fillRect(area, Qt::black);
            p.drawImage(fit(frozen_.deviceIndependentSize().toSize(), area), frozen_);
        }
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
#ifdef LSW_HAVE_REMOTEWIN
    if (session_ == nullptr || !session_->framed || !session_->monitor) return;
    const QImage frame = session_->monitor->view().grab().toImage();
    if (frame.isNull()) return;
    frozen_ = frame.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_RGB32);
    frozen_.setDevicePixelRatio(frame.devicePixelRatio());
    QPainter p{&frozen_};
    p.fillRect(QRect(QPoint(0, 0), frozen_.size()), kPausedDim);
#endif
}

void DesktopPreview::stop() {
    if (session_ == nullptr) return;
#ifdef LSW_HAVE_REMOTEWIN
    if (session_->monitor) session_->monitor->view().hide();
#endif
    // A frame or disconnect signal may be on the stack. Retire the session
    // after that dispatch, without closing any Windows application.
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
    // Paused, a capture is only for the frame it starts on.
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
    const QString socket = vm::ExportSocket::path();
    if (!socket.isEmpty()) session->frames.open(socket.toStdString());
    if (!session->link.listen(0, QHostAddress::LocalHost)) {
        fail(QStringLiteral("cannot listen for the guest"));
        return;
    }
    session->monitor = std::make_unique<rwin::host::MonitorSession>(
        session->link, session->frames, QStringLiteral("Guest preview"), session, this);
    auto& view = session->monitor->view();
    view.set_interactive(false);
    lay_out();
    view.show();
    if (!isVisible()) {
        stop();
        return;
    }
    connect(&session->link, &rwin::host::GuestLink::mismatched, this,
            [this, session](const QString& reason) {
                if (session_ == session) fail(reason);
            });
    connect(&session->link, &rwin::host::GuestLink::disconnected, this, [this, session] {
        if (session_ == session) fail(QStringLiteral("guest capture disconnected"));
    });
    connect(session->monitor.get(), &rwin::host::MonitorSession::finished, this, [this, session] {
        if (session_ == session) fail(QStringLiteral("guest monitor capture stopped"));
    });
    connect(session->monitor.get(), &rwin::host::MonitorSession::ready, this, [session] {
        session->deadline.stop();
    });
    const auto framed = [this, session](const QString& route) {
        if (session_ != session) return;
        if (!session->framed) {
            session->framed = true;
            session->deadline.stop();
            session->settle.start(kSettleMs);
        }
        if (!paused_ && status_ != route) note(route);
        surface_->update();
    };
    connect(&session->frames, &rwin::host::SharedFrameFeed::frame_ready, this,
            [framed](quint64 window, const rwin::host::SharedFrame&) {
                if (window == rwin::kMonitorWindow) framed(QStringLiteral("WGC, GPU"));
            });
    connect(&session->link, &rwin::host::GuestLink::frame_arrived, this,
            [framed](quint64 window, const QImage&) {
                if (window == rwin::kMonitorWindow) framed(QStringLiteral("WGC, pixels"));
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
                       {{"port", session->link.port()}, {"desktop", true}}, session,
                       [this, session](const link::AgentReply& reply) {
                           if (session_ == session && !reply.ok) {
                               fail(QString::fromStdString(reply.error));
                           }
                       });
#endif
}

}  // namespace lsw::host::manager
