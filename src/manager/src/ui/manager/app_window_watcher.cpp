#include "lsw/manager/ui/manager/app_window_watcher.hpp"

#include "lsw/manager/ui/manager/window_view.hpp"
#include "lsw/manager/vm/agent_rpc_client.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

namespace lsw::manager::ui::manager {

namespace {

constexpr int kPollMs = 50;  // ~20 Hz: popups still appear ~instantly,
                             // far less load than 50 Hz EnumWindows +
                             // RPC competing with 30 fps WGC capture
constexpr qint64 kIdleQuitMs = 30000;  // shut down 30 s after the app stops showing windows

struct GuestWindow {
    quint64 hwnd;
    quint64 owner;     // GW_OWNER hwnd (0 = none) — for popup parenting
    quint32 pid;
    QString title;
    QString exe_path;
    int screen_x = 0;  // client-area origin in the guest's screen space
    int screen_y = 0;
};

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

QList<GuestWindow> rpc_list_windows(vm::AgentRpcClient& rpc) {
    QList<GuestWindow> out;
    const auto reply = rpc.call(QByteArrayLiteral("{\"cmd\":\"list_windows\"}"));
    if (reply.isEmpty()) {
        return out;
    }
    try {
        const auto j = nlohmann::json::parse(reply.constData(),
                                              reply.constData() + reply.size());
        if (!j.contains("windows") || !j["windows"].is_array()) {
            return out;
        }
        for (const auto& w : j["windows"]) {
            GuestWindow gw;
            gw.hwnd = w.value("hwnd", std::uint64_t{0});
            gw.owner = w.value("owner", std::uint64_t{0});
            gw.pid = static_cast<quint32>(w.value("pid", std::uint64_t{0}));
            gw.title = QString::fromStdString(w.value("title", std::string{}));
            gw.exe_path = QString::fromStdString(w.value("exe_path", std::string{}));
            gw.screen_x = w.value("screen_x", 0);
            gw.screen_y = w.value("screen_y", 0);
            out.append(gw);
        }
    } catch (const nlohmann::json::exception&) {
    }
    return out;
}

}  // namespace

AppWindowWatcher::AppWindowWatcher(vm::AgentRpcClient& rpc, quint32 pid,
                                     QString exe_basename, QObject* parent)
    : QObject(parent), rpc_(rpc), pid_(pid),
      exe_basename_(exe_basename.toLower()) {
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, &AppWindowWatcher::on_tick);
    last_seen_unix_ = QDateTime::currentMSecsSinceEpoch();
}

void AppWindowWatcher::start() {
    if (!timer_.isActive()) {
        timer_.start();
        on_tick();  // first probe immediately
    }
}

void AppWindowWatcher::stop() {
    timer_.stop();
    Q_EMIT exhausted(pid_);
}

void AppWindowWatcher::on_tick() {
    if (tick_in_flight_) {
        return;
    }
    tick_in_flight_ = true;
    auto* watcher = new QFutureWatcher<QList<GuestWindow>>(this);
    connect(watcher, &QFutureWatcher<QList<GuestWindow>>::finished, this,
            [this, watcher] {
        tick_in_flight_ = false;
        const auto wins = watcher->result();
        watcher->deleteLater();

        // First tick establishes the baseline: every HWND that
        // already existed before our launch. The exe-name fallback
        // below excludes these so it can't adopt a pre-existing
        // instance of the same app.
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
            // Instance identified — strict PID scoping from here on,
            // never name-adopt again (that is what multiplied mirrors
            // across launches).
            for (const auto& w : wins) {
                if (w.pid == locked_pid_) {
                    matches.insert(w.hwnd, w.title);
                }
            }
        } else {
            matches = select_matches(wins, pid_, exe_basename_,
                                     baseline_hwnds_);
            // First time we have our window(s): lock to their real
            // PID. Prefer the launched PID if it actually matched
            // (genuine Win32); otherwise adopt the PID of a
            // name-matched window (alias/packaged case).
            if (!matches.isEmpty()) {
                quint32 chosen = 0;
                for (auto it = matches.constBegin();
                     it != matches.constEnd(); ++it) {
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
        // entries persist even after the WindowView is destroyed
        // (QPointer goes null) — that's how we know not to respawn a
        // mirror for a window the user just closed while the guest
        // is still processing WM_CLOSE.
        for (auto it = matches.constBegin(); it != matches.constEnd(); ++it) {
            const quint64 hwnd = it.key();
            if (views_.contains(hwnd)) {
                continue;
            }
            const GuestWindow gw = by_hwnd.value(hwnd);
            // If this window is owned by another window we're already
            // mirroring (e.g. Notepad's Find / Settings popup), parent
            // the new mirror to that owner and offset it by the
            // guest-side relative position so it lands where the app
            // put it instead of wherever the host WM drops a new
            // top-level. (See WindowView — best-effort under Wayland.)
            WindowView* owner_view = nullptr;
            QPoint rel_guest_px;
            quint64 owner_hwnd = 0;
            if (gw.owner != 0 && views_.contains(gw.owner)) {
                owner_hwnd = gw.owner;
            } else if (!views_.isEmpty()) {
                // Fallback: the agent reports owner via GA_ROOTOWNER,
                // but for some classic popups (ComboLBox dropdowns
                // most notoriously) the chain doesn't resolve to a
                // window we mirror. If we know any window for this
                // app instance, anchor the new popup to it instead of
                // letting it become a free, decorated top-level —
                // that path produced the "wrong place + still
                // decorated" combobox dropdowns.
                for (auto v = views_.constBegin(); v != views_.constEnd();
                     ++v) {
                    if (!v.value().isNull() && by_hwnd.contains(v.key())) {
                        owner_hwnd = v.key();
                        break;
                    }
                }
            }
            if (owner_hwnd != 0) {
                owner_view = views_.value(owner_hwnd).data();
                const GuestWindow ow = by_hwnd.value(owner_hwnd);
                rel_guest_px = QPoint(gw.screen_x - ow.screen_x,
                                      gw.screen_y - ow.screen_y);
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
                // enumeration) — force the mirror down. Plain close()
                // now only *requests* a guest close and keeps the
                // mirror alive until the window really vanishes.
                v->guest_closed();
            }
        }

        // Idle shutdown: no live mirrors AND no matching guest windows
        // for kIdleQuitMs → self-destruct.
        if (!matches.isEmpty() || !views_.isEmpty()) {
            last_seen_unix_ = QDateTime::currentMSecsSinceEpoch();
        } else if (QDateTime::currentMSecsSinceEpoch() - last_seen_unix_ > kIdleQuitMs) {
            stop();
        }
    });
    watcher->setFuture(
        QtConcurrent::run([this] { return rpc_list_windows(rpc_); }));
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

}  // namespace lsw::manager::ui::manager
