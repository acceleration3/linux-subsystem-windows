#include "lsw/host/mirror/guest_mirrors.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QFileInfo>
#include <QMetaObject>

#include <cstdint>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace lsw::host::mirror {

namespace {
constexpr int kSmallestWindow = 32;
constexpr qsizetype kMostParked = 64;
}

GuestMirrors::GuestWindow::GuestWindow(const nlohmann::json& row)
    : pid(row.value("pid", quint32{0})), root_pid(row.value("root_pid", quint32{0})),
      image(QString::fromStdString(row.value("exe_path", std::string{}))) {
    const bool placed = row.value("minimized", false) ||
                        (row.value("on_screen", false) && row.value("client_w", 0) >= kSmallestWindow &&
                         row.value("client_h", 0) >= kSmallestWindow);
    mirrorable = pid != 0 && row.value("has_caption", false) && row.value("owner", std::uint64_t{0}) == 0 &&
                 placed;
}

GuestMirrors::GuestMirrors(link::AgentRpcClient& rpc, QObject* parent) : QObject(parent), rpc_(rpc) {
    appeared_sub_ = rpc_.subscribe(linkproto::topic::kWindowAppeared, [this](const nlohmann::json& row) {
        QMetaObject::invokeMethod(
            this, [this, window = GuestWindow(row)] { appeared(window); }, Qt::QueuedConnection);
    });
}

QString GuestMirrors::window_key(quint32 pid) const {
    return QStringLiteral("window:%1:%2").arg(rpc_.port()).arg(pid);
}

QString GuestMirrors::output() const {
    return output_source_ ? output_source_() : QString();
}

void GuestMirrors::open_desktop(const QString& title) {
    if (desktop_) {
        desktop_->raise(QString());
        return;
    }
    auto* desktop = WindowMirror::of_desktop(rpc_, title, output(), this);
    connect(desktop, &WindowMirror::finished, this, [this, desktop] {
        desktop->deleteLater();
        if (desktop_ == desktop) {
            desktop_.clear();
            Q_EMIT desktop_closed();
            resume();
        }
    });
    connect(desktop, &WindowMirror::room_settled, this, &GuestMirrors::desktop_resized);
    connect(desktop, &WindowMirror::failed, this, &GuestMirrors::desktop_failed);
    connect(desktop, &WindowMirror::ready, this, &GuestMirrors::desktop_ready);
    desktop_ = desktop;
    if (!desktop->start()) {
        desktop_.clear();
        desktop->deleteLater();
        return;
    }
    if (desktop_ == desktop) {
        Q_EMIT desktop_opened();
        suspend();
    }
}

void GuestMirrors::suspend() {
    suspended_ = true;
    parked_.clear();
    shown_by_.clear();
    QStringList applications;
    for (auto it = entries_.begin(); it != entries_.end();) {
        delete it->mirror.data();
        if (it->unowned) {
            it = entries_.erase(it);
        } else {
            applications << it.key();
            it = std::next(it);
        }
    }
    LSW_INFO("mirror: the desktop is open; {} application mirrors closed until it closes",
             applications.size());
    for (const QString& identity : applications) {
        Q_EMIT shown(identity);
    }
}

void GuestMirrors::resume() {
    if (!suspended_) {
        return;
    }
    LSW_INFO("mirror: the desktop closed; every window is mirrored again");
    rpc_.request_async(linkproto::cmd::kListWindows, nlohmann::json::object(), this,
                       [this](const link::AgentReply& reply) {
                           if (desktop_ || !suspended_) {
                               return;
                           }
                           suspended_ = false;
                           if (!reply.ok) {
                               LSW_WARN("mirror: the guest's windows could not be listed: {}", reply.error);
                           }
                           const auto windows = reply.body.find("windows");
                           remirror(windows != reply.body.end() && windows->is_array() ? *windows
                                                                                       : nlohmann::json::array());
                       });
}

void GuestMirrors::remirror(const nlohmann::json& rows) {
    QList<GuestWindow> others;
    for (const auto& row : rows) {
        const GuestWindow window(row);
        const QString identity = application_of(window.owner_pid());
        if (identity.isEmpty()) {
            others << window;
        } else {
            resume(identity);
        }
    }
    for (const QString& identity : entries_.keys()) {
        if (!entries_.contains(identity) || mirror(identity) != nullptr) {
            continue;
        }
        if (!entries_[identity].application.aumid.isEmpty()) {
            resume(identity);
            continue;
        }
        LSW_INFO("mirror: {} shows no window any more", identity.toStdString());
        remove(identity);
        Q_EMIT closed(identity);
    }
    for (const GuestWindow& window : others) {
        appeared(window);
    }
}

void GuestMirrors::resume(const QString& identity) {
    if (mirror(identity) == nullptr) {
        entries_[identity].resumed = true;
        start(identity);
    }
}

void GuestMirrors::appeared(const GuestWindow& window) {
    if (desktop_) {
        return;
    }
    if (expected_launches_ > 0) {
        if (parked_.size() < kMostParked) {
            parked_.append(window);
        }
        return;
    }
    adopt(window);
}

void GuestMirrors::adopt(const GuestWindow& window) {
    const quint32 pid = window.owner_pid();
    if (const QString identity = application_of(pid); !identity.isEmpty()) {
        resume(identity);
        return;
    }
    const QString identity = window_key(pid);
    if (!window.mirrorable || mirror(identity) != nullptr) {
        return;
    }
    Entry entry;
    entry.application.pid = pid;
    entry.application.log_name =
        QFileInfo(QString(window.image).replace(QLatin1Char('\\'), QLatin1Char('/'))).completeBaseName();
    entry.unowned = true;
    entries_.insert(identity, entry);
    LSW_INFO("mirror: pid {} '{}' shows a window no mirror owns; it gets its own", pid,
             window.image.toStdString());
    start(identity);
}

QString GuestMirrors::application_of(quint32 pid) const {
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        if (!it->unowned && it->application.pid == pid) {
            return it.key();
        }
    }
    return {};
}

void GuestMirrors::expect_launch() {
    ++expected_launches_;
}

void GuestMirrors::launch_settled() {
    if (expected_launches_ == 0 || --expected_launches_ != 0) {
        return;
    }
    for (const GuestWindow& window : std::exchange(parked_, {})) {
        appeared(window);
    }
}

void GuestMirrors::show(const QString& identity, const WindowMirror::Application& application,
                        bool keep_previous) {
    if (const QString window = window_key(application.pid); entries_.contains(window)) {
        forget(window);
    }
    if (const auto previous = entries_.constFind(identity); previous != entries_.cend()) {
        const Entry kept = *previous;
        remove(identity);
        if (keep_previous && !kept.mirror.isNull() && kept.application.pid != application.pid) {
            const QString beside = identity + QLatin1Char('#') + QString::number(kept.application.pid);
            entries_.insert(beside, kept);
            kept.mirror->disconnect(this);
            connect(kept.mirror, &WindowMirror::finished, this, [this, beside, mirror = kept.mirror.data()] {
                if (this->mirror(beside) == mirror) {
                    remove(beside);
                }
            });
        } else if (!kept.mirror.isNull()) {
            kept.mirror->deleteLater();
        }
    }
    Entry entry;
    entry.application = application;
    entries_.insert(identity, entry);
    if (desktop_) {
        LSW_INFO("mirror: pid {} is shown by the open desktop; its own mirror starts when the desktop closes",
                 application.pid);
        desktop_->raise(application.activation_token);
        entries_[identity].application.activation_token.clear();
        Q_EMIT shown(identity);
        return;
    }
    start(identity);
}

void GuestMirrors::start(const QString& identity) {
    WindowMirror::Application application = entries_[identity].application;
    application.output = output();
    entries_[identity].application.activation_token.clear();
    auto* mirror = WindowMirror::of_application(rpc_, application, this);
    entries_[identity].mirror = mirror;
    connect(mirror, &WindowMirror::app_identified, this, [this, identity, mirror](const QString& claim) {
        if (this->mirror(identity) == mirror) {
            identified(identity, claim);
        }
    });
    connect(mirror, &WindowMirror::ready, this, [this, identity, mirror] {
        if (this->mirror(identity) == mirror) {
            entries_[identity].resumed = false;
            Q_EMIT shown(identity);
        }
    });
    connect(mirror, &WindowMirror::failed, this, [this, identity, mirror](const QString& reason) {
        if (this->mirror(identity) != mirror) {
            return;
        }
        const bool resumed = entries_[identity].resumed;
        remove(identity);
        if (resumed) {
            Q_EMIT closed(identity);
        } else {
            Q_EMIT failed(identity, reason);
        }
    });
    connect(mirror, &WindowMirror::finished, this, [this, identity, mirror] {
        if (this->mirror(identity) == mirror) {
            remove(identity);
            Q_EMIT closed(identity);
        }
    });
    connect(mirror, &WindowMirror::finished, mirror, &QObject::deleteLater);
    if (!mirror->start()) {
        mirror->deleteLater();
    }
}

void GuestMirrors::identified(const QString& identity, const QString& claim) {
    const QString holder = shown_by_.value(claim);
    if (holder.isEmpty() || holder == identity || mirror(holder) == nullptr) {
        shown_by_.insert(claim, identity);
        return;
    }
    const bool takes_over = entries_[holder].unowned && !entries_[identity].unowned;
    const QString loser = takes_over ? holder : identity;
    const QString keeper = takes_over ? identity : holder;
    LSW_INFO("mirror: {} and {} show the windows of {}; {} is kept", identity.toStdString(),
             holder.toStdString(), claim.toStdString(), keeper.toStdString());
    const QPointer<WindowMirror> duplicate = mirror(loser);
    remove(loser);
    if (takes_over) {
        shown_by_.insert(claim, identity);
    } else {
        mirror(keeper)->raise(QString());
    }
    Q_EMIT merged(loser, keeper);
    if (!duplicate.isNull()) {
        duplicate->deleteLater();
    }
}

void GuestMirrors::remove(const QString& identity) {
    entries_.remove(identity);
    for (auto it = shown_by_.begin(); it != shown_by_.end();) {
        it = it.value() == identity ? shown_by_.erase(it) : std::next(it);
    }
}

void GuestMirrors::forget(const QString& identity) {
    const QPointer<WindowMirror> gone = mirror(identity);
    remove(identity);
    if (!gone.isNull()) {
        gone->deleteLater();
    }
}

void GuestMirrors::raise(const QString& identity, const QString& activation_token) const {
    if (auto* shown = mirror(identity); shown != nullptr) {
        shown->raise(activation_token);
    } else if (desktop_ && entries_.contains(identity)) {
        desktop_->raise(activation_token);
    }
}

bool GuestMirrors::has(const QString& identity) const {
    const auto entry = entries_.constFind(identity);
    return entry != entries_.cend() && (!entry->mirror.isNull() || suspended_);
}

WindowMirror* GuestMirrors::mirror(const QString& identity) const {
    const auto entry = entries_.constFind(identity);
    return entry == entries_.cend() ? nullptr : entry->mirror.data();
}

quint32 GuestMirrors::pid(const QString& identity) const {
    const auto entry = entries_.constFind(identity);
    return entry == entries_.cend() ? 0 : entry->application.pid;
}

QString GuestMirrors::identity_of(quint32 pid, quint64 pid_created) const {
    for (auto it = entries_.cbegin(); it != entries_.cend(); ++it) {
        if (!it->unowned && it->application.pid == pid && it->application.pid_created == pid_created) {
            return it.key();
        }
    }
    return {};
}

}
