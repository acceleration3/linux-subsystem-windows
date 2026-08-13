#include "lsw/host/manager/app_window_watcher.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/window_view.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QDateTime>

#include <algorithm>

#include <nlohmann/json.hpp>

namespace lsw::host::manager {

namespace {

using link::GuestWindow;

constexpr qint64 kIdleQuitMs = 30000;  // shut down 30 s after the app stops showing windows

// Basename of a Windows path, lowercased. The agent reports
// ``exe_path`` with backslashes; handle forward slashes too for
// safety.
QString exe_basename_lower(const QString& path) {
    const qsizetype slash = std::max(path.lastIndexOf(QLatin1Char('\\')),
                                     path.lastIndexOf(QLatin1Char('/')));
    return (slash < 0 ? path : path.mid(slash + 1)).toLower();
}

// Select the HWNDs (→ titles) this watcher owns out of an enumeration.
// Primary scoping is the exact launched PID — that separates
// simultaneous instances of a genuine Win32 app. The exe-name path is
// a fallback only: App Execution Aliases / packaged apps launch a
// throwaway stub, so the real window lives under a different PID and
// the PID filter finds nothing. The fallback matches by executable
// basename but skips ``baseline`` HWNDs (windows that existed before
// our launch) so it can't hijack a pre-existing instance, and is only
// consulted when the PID match is empty.
QHash<quint64, QString> select_matches(const QList<GuestWindow>& wins,
                                        quint32 pid,
                                        const QString& exe_basename_lc,
                                        const QSet<quint64>& baseline) {
    QHash<quint64, QString> matches;
    for (const auto& w : wins) {
        if (w.pid == pid) {
            matches.insert(w.hwnd, w.title);
        }
    }
    if (!matches.isEmpty() || exe_basename_lc.isEmpty()) {
        return matches;
    }
    for (const auto& w : wins) {
        if (!baseline.contains(w.hwnd)
            && exe_basename_lower(w.exe_path) == exe_basename_lc) {
            matches.insert(w.hwnd, w.title);
        }
    }
    return matches;
}

}  // namespace

AppWindowWatcher::AppWindowWatcher(link::AgentRpcClient& rpc,
                                     link::GuestWindowModel& windows, quint32 pid,
                                     QString exe_basename, QObject* parent)
    : QObject(parent), rpc_(rpc), windows_(windows), pid_(pid),
      exe_basename_(exe_basename.toLower()) {
    last_seen_unix_ = QDateTime::currentMSecsSinceEpoch();
}

void AppWindowWatcher::start() {
    if (subscribed_) {
        return;
    }
    // Ask the agent to emit UIA lifecycle events for our PID. The events
    // land on the shared window model, which turns them into a refresh —
    // this watcher only ever sees the resulting enumeration. That is
    // deliberate: the event payload lacks the popup-parent and
    // screen-position metadata the matching below needs, so there is one
    // code path rather than two that have to agree.
    const QByteArray sub_reply = rpc_.call(nlohmann::json{
        {"cmd", linkproto::cmd::kUiaSubscribeLifecycle},
        {"params", {{"pid", static_cast<std::uint64_t>(pid_)}}}});
    if (!sub_reply.isEmpty()) {
        try {
            const auto j = nlohmann::json::parse(
                sub_reply.constData(), sub_reply.constData() + sub_reply.size());
            uia_sub_id_ = j.value("sub_id", std::uint64_t{0});
        } catch (const nlohmann::json::exception&) {
        }
    }

    connect(&windows_, &link::GuestWindowModel::updated, this,
            &AppWindowWatcher::on_windows);
    windows_.subscribe();
    subscribed_ = true;

    // Act on what the model already knows, then ask for a fresh
    // enumeration — a launch is exactly the moment its answer changes.
    if (!windows_.snapshot().isEmpty()) {
        on_windows(windows_.snapshot());
    }
    windows_.refresh_now();
}

void AppWindowWatcher::stop() {
    if (subscribed_) {
        disconnect(&windows_, &link::GuestWindowModel::updated, this,
                   &AppWindowWatcher::on_windows);
        windows_.unsubscribe();
        subscribed_ = false;
    }
    if (uia_sub_id_ != 0) {
        // Fire-and-forget: the watcher is going away, we don't care
        // about the reply. ``post_oneway`` keeps the disconnect path off
        // a blocking round trip on the GUI thread.
        rpc_.post_oneway(nlohmann::json{
            {"cmd", linkproto::cmd::kUiaUnsubscribeLifecycle},
            {"params", {{"sub_id", uia_sub_id_}}}});
        uia_sub_id_ = 0;
    }
    Q_EMIT exhausted(pid_);
}

void AppWindowWatcher::on_windows(const QList<link::GuestWindow>& wins) {
    // The first enumeration establishes the baseline: every HWND that
    // already existed before our launch. The exe-name fallback excludes
    // these so it can't adopt a pre-existing instance.
    if (!baseline_captured_) {
        for (const auto& w : wins) {
            baseline_hwnds_.insert(w.hwnd);
        }
        baseline_captured_ = true;
    }

    // Index every enumerated window (popup owner lookup + locking).
    QHash<quint64, GuestWindow> by_hwnd;
    for (const auto& w : wins) {
        by_hwnd.insert(w.hwnd, w);
    }

    QHash<quint64, QString> matches;
    if (locked_) {
        // Instance identified — strict PID scoping from here on, never
        // name-adopt again (that is what multiplied mirrors across
        // launches).
        for (const auto& w : wins) {
            if (w.pid == locked_pid_) {
                matches.insert(w.hwnd, w.title);
            }
        }
    } else {
        matches = select_matches(wins, pid_, exe_basename_, baseline_hwnds_);
        // First time we have our window(s): lock to their real PID.
        // Prefer the launched PID if it actually matched (genuine
        // Win32); otherwise adopt the PID of a name-matched window
        // (alias/packaged case).
        if (!matches.isEmpty()) {
            quint32 chosen = 0;
            for (auto it = matches.constBegin(); it != matches.constEnd(); ++it) {
                const quint32 wp = by_hwnd.value(it.key()).pid;
                if (wp == pid_) {  // genuine launched process wins
                    chosen = wp;
                    break;
                }
                if (chosen == 0) {
                    chosen = wp;
                }
            }
            if (chosen != 0) {
                locked_ = true;
                locked_pid_ = chosen;
            }
        }
    }

    // Spawn new mirrors for HWNDs we haven't seen yet. ``views_``
    // entries persist even after the WindowView is destroyed (QPointer
    // goes null) — that's how we know not to respawn a mirror for a
    // window the user just closed while the guest is still processing
    // WM_CLOSE.
    for (auto it = matches.constBegin(); it != matches.constEnd(); ++it) {
        const quint64 hwnd = it.key();
        if (views_.contains(hwnd)) {
            continue;
        }
        const GuestWindow gw = by_hwnd.value(hwnd);
        // If this window is owned by another window we're already
        // mirroring (e.g. Notepad's Find / Settings popup), parent the
        // new mirror to that owner and offset it by the guest-side
        // relative position so it lands where the app put it instead of
        // wherever the host WM drops a new top-level. (See WindowView —
        // best-effort under Wayland.)
        WindowView* owner_view = nullptr;
        QPoint rel_guest_px;
        quint64 owner_hwnd = 0;
        if (gw.owner != 0 && views_.contains(gw.owner)) {
            owner_hwnd = gw.owner;
        } else if (!views_.isEmpty()) {
            // Fallback: the agent reports owner via GA_ROOTOWNER, but
            // for some classic popups (ComboLBox dropdowns most
            // notoriously) the chain doesn't resolve to a window we
            // mirror. If we know any window for this app instance,
            // anchor the new popup to it instead of letting it become a
            // free, decorated top-level — that path produced the "wrong
            // place + still decorated" combobox dropdowns.
            for (auto v = views_.constBegin(); v != views_.constEnd(); ++v) {
                if (!v.value().isNull() && by_hwnd.contains(v.key())) {
                    owner_hwnd = v.key();
                    break;
                }
            }
        }
        if (owner_hwnd != 0) {
            owner_view = views_.value(owner_hwnd).data();
            const GuestWindow ow = by_hwnd.value(owner_hwnd);
            rel_guest_px = QPoint(gw.screen_x - ow.screen_x, gw.screen_y - ow.screen_y);
        }
        spawn_view(gw.hwnd, it.value(), owner_view, rel_guest_px);
    }

    // Tear down mirrors whose underlying HWND is gone.
    QList<quint64> stale;
    for (auto it = views_.constBegin(); it != views_.constEnd(); ++it) {
        if (!matches.contains(it.key())) {
            stale.append(it.key());
        }
    }
    for (auto hwnd : stale) {
        QPointer<WindowView> v = views_.take(hwnd);
        if (v != nullptr) {
            // The guest window is verifiably gone (absent from the
            // enumeration) — force the mirror down. Plain close() now
            // only *requests* a guest close and keeps the mirror alive
            // until the window really vanishes.
            v->guest_closed();
        }
    }

    // Idle shutdown: no live mirrors AND no matching guest windows for
    // kIdleQuitMs → self-destruct.
    if (!matches.isEmpty() || !views_.isEmpty()) {
        last_seen_unix_ = QDateTime::currentMSecsSinceEpoch();
    } else if (QDateTime::currentMSecsSinceEpoch() - last_seen_unix_ > kIdleQuitMs) {
        stop();
    }
}

void AppWindowWatcher::spawn_view(std::uint64_t hwnd, const QString& title,
                                   WindowView* relative_to,
                                   QPoint rel_guest_px) {
    auto* view = new WindowView(rpc_, hwnd, title, relative_to, rel_guest_px);
    // Popups show immediately (frameless, instant placement); main
    // mirrors show themselves once window_props has been fetched and
    // applied, so we don't open at a Qt-default size and shrink the
    // guest before we even know what the right size is.
    if (relative_to != nullptr) {
        view->show();
    }
    views_.insert(hwnd, view);
}

}  // namespace lsw::host::manager
