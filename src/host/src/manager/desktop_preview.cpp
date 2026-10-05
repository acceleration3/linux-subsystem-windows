#include "lsw/host/manager/desktop_preview.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/vm/export_socket.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>

#ifdef LSW_HAVE_REMOTEWIN
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/monitor_session.hpp"
#include "proto/message_id.hpp"
#endif

#include <memory>

namespace lsw::host::manager {

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
};

DesktopPreview::DesktopPreview(link::AgentRpcClient& rpc, QWidget* parent)
    : QWidget(parent), rpc_(rpc), layout_(new QVBoxLayout(this)), message_(new QLabel(this)),
      play_(new QPushButton(QStringLiteral("Play preview"), this)),
      retry_(new QPushButton(QStringLiteral("Retry preview"), this)) {
    setObjectName(QStringLiteral("DesktopPreview"));
    layout_->setContentsMargins(0, 0, 0, 0);
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    layout_->addWidget(message_, 1);
    auto* controls = new QHBoxLayout;
    controls->addStretch();
    controls->addWidget(play_);
    controls->addWidget(retry_);
    controls->addStretch();
    layout_->addLayout(controls);
    play_->setObjectName(QStringLiteral("PreviewPlayback"));
    play_->setEnabled(false);
    play_->setToolTip(QStringLiteral("Play captures the desktop with WGC; pause stops capture."));
    retry_->setObjectName(QStringLiteral("PreviewRetry"));
    retry_->hide();
    connect(play_, &QPushButton::clicked, this, [this] { set_paused(!paused_); });
    connect(retry_, &QPushButton::clicked, this, [this] {
        failed_ = false;
        start();
    });
    note(QStringLiteral("WGC, waiting for guest agent"));
}

DesktopPreview::~DesktopPreview() {
    stop();
}

void DesktopPreview::note(const QString& status) {
    status_ = status;
    message_->setText(status);
    Q_EMIT status_changed(status);
}

void DesktopPreview::clear() {
    available_ = false;
    failed_ = false;
    set_paused(true);
    stop();
    play_->setEnabled(false);
    retry_->hide();
    note(QStringLiteral("WGC, waiting for guest agent"));
}

void DesktopPreview::set_available(bool available) {
    if (available_ == available)
        return;
    available_ = available;
    failed_ = false;
    stop();
    play_->setEnabled(available_);
    retry_->hide();
    note(!available_ ? QStringLiteral("WGC, waiting for guest agent")
         : paused_   ? QStringLiteral("WGC, paused")
                     : QStringLiteral("WGC, preview hidden"));
    if (available_)
        start();
}

void DesktopPreview::set_paused(bool paused) {
    if (paused_ == paused)
        return;
    paused_ = paused;
    play_->setText(paused_ ? QStringLiteral("Play preview") : QStringLiteral("Pause preview"));
    if (paused_) {
        stop();
        if (available_)
            note(QStringLiteral("WGC, paused"));
    } else {
        failed_ = false;
        start();
    }
    Q_EMIT paused_changed(paused_);
}

void DesktopPreview::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    start();
}

void DesktopPreview::hideEvent(QHideEvent* event) {
    // Adding the first QOpenGLWidget can recreate the top-level native
    // window. Its temporary hide/show is not a request to restart capture.
    if (!starting_)
        stop();
    QWidget::hideEvent(event);
}

void DesktopPreview::stop() {
    if (session_ == nullptr)
        return;
#ifdef LSW_HAVE_REMOTEWIN
    if (session_->monitor) {
        auto& view = session_->monitor->view();
        layout_->removeWidget(&view);
        view.hide();
    }
#endif
    // A frame or disconnect signal may be on the stack. Retire the session
    // after that dispatch, without closing any Windows application.
    session_->deadline.stop();
    session_->deleteLater();
    session_ = nullptr;
    message_->show();
    if (available_ && !failed_)
        note(paused_ ? QStringLiteral("WGC, paused") : QStringLiteral("WGC, preview hidden"));
}

void DesktopPreview::fail(const QString& reason) {
    failed_ = true;
    stop();
    note(QStringLiteral("WGC preview: %1").arg(reason));
    retry_->setVisible(available_);
}

void DesktopPreview::start() {
    if (!available_ || !isVisible() || paused_ || failed_ || starting_ || session_ != nullptr)
        return;
    const QScopedValueRollback<bool> starting(starting_, true);
#ifndef LSW_HAVE_REMOTEWIN
    fail(QStringLiteral("RemoteWin was not built. Enable LSW_BUILD_REMOTEWIN."));
#else
    auto* session = new Session(this);
    session_ = session;
    const QString socket = vm::ExportSocket::path();
    if (!socket.isEmpty())
        session->frames.open(socket.toStdString());
    if (!session->link.listen(0, QHostAddress::LocalHost)) {
        fail(QStringLiteral("cannot listen for the guest"));
        return;
    }
    session->monitor = std::make_unique<rwin::host::MonitorSession>(
        session->link, session->frames, QStringLiteral("Guest preview"), session, this);
    session->monitor->view().set_interactive(false);
    layout_->insertWidget(1, &session->monitor->view(), 1);
    if (!isVisible()) {
        stop();
        return;
    }
    message_->hide();
    retry_->hide();
    note(QStringLiteral("WGC, connecting"));
    connect(&session->link, &rwin::host::GuestLink::mismatched, this,
            [this, session](const QString& reason) {
                if (session_ == session)
                    fail(reason);
            });
    connect(&session->link, &rwin::host::GuestLink::disconnected, this, [this, session] {
        if (session_ == session)
            fail(QStringLiteral("guest capture disconnected"));
    });
    connect(session->monitor.get(), &rwin::host::MonitorSession::finished, this, [this, session] {
        if (session_ == session)
            fail(QStringLiteral("guest monitor capture stopped"));
    });
    connect(session->monitor.get(), &rwin::host::MonitorSession::ready, this, [this, session] {
        if (session_ == session)
            session->deadline.stop();
    });
    connect(&session->frames, &rwin::host::SharedFrameFeed::frame_ready, this,
            [this, session](quint64 window, const rwin::host::SharedFrame&) {
                if (session_ == session && window == rwin::kMonitorWindow &&
                    status_ != QStringLiteral("WGC, GPU"))
                    note(QStringLiteral("WGC, GPU"));
            });
    connect(&session->link, &rwin::host::GuestLink::frame_arrived, this,
            [this, session](quint64 window, const QImage&) {
                if (session_ == session && window == rwin::kMonitorWindow &&
                    status_ != QStringLiteral("WGC, pixels"))
                    note(QStringLiteral("WGC, pixels"));
            });
    session->deadline.setSingleShot(true);
    connect(&session->deadline, &QTimer::timeout, this, [this, session] {
        if (session_ == session)
            fail(QStringLiteral("no desktop frame within 60 seconds"));
    });
    session->deadline.start(60000);
    rpc_.request_async(linkproto::cmd::kStartWindowMirror,
                       {{"port", session->link.port()}, {"desktop", true}}, session,
                       [this, session](const link::AgentReply& reply) {
                           if (session_ == session && !reply.ok)
                               fail(QString::fromStdString(reply.error));
                       });
#endif
}

}  // namespace lsw::host::manager
