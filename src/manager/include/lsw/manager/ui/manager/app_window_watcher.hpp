#pragma once

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

#include <cstdint>

namespace lsw::manager::vm {
class AgentRpcClient;
}

namespace lsw::manager::ui::manager {

class WindowView;

// Per-launch window watcher. Polls the agent's ``list_windows`` RPC
// every ~1.5s, filters to windows owned by ``pid_``, and:
//   * spawns a ``WindowView`` for every new HWND it sees
//   * closes the ``WindowView`` when the underlying HWND disappears
//
// Closing a mirror on the host side fires a ``close_window`` RPC that
// posts WM_CLOSE to the guest window — closing the mirror IS closing
// the window, no separate "the user closed this, don't respawn"
// blacklist needed. The QPointer entry left behind in ``views_``
// after WA_DeleteOnClose tears the view down keeps us from spawning
// a duplicate while the guest window is going through its WM_CLOSE
// handler. Once the guest finally drops the HWND, the entry is
// pruned by the next tick's stale-cleanup pass.
//
// Scoped to a single launched-instance PID — multiple simultaneous
// launches of the same app each get their own watcher and their own
// mirror set.
//
// PID scoping breaks for App Execution Aliases / packaged apps
// (Win11 ``C:\Windows\system32\notepad.exe`` etc.): CreateProcessW
// spawns a throwaway stub whose PID we capture, but the real window
// is owned by the packaged process under a *different* PID, so
// ``w.pid == pid_`` never matches. As a fallback the watcher also
// matches by the launched executable's basename — but only for
// windows that did NOT exist at the first tick (so it can't hijack a
// pre-existing instance the user already had open), and only when the
// PID yields no windows at all (genuine Win32 apps keep exact-PID
// scoping, which correctly separates simultaneous instances).
//
// Lives until ``stop()`` is called or it has no live mirrors and
// hasn't seen any matching window in ``kIdleQuitMs`` ms (so it cleans
// itself up after the user closes everything and the guest has fully
// quit that instance).
class AppWindowWatcher : public QObject {
    Q_OBJECT
public:
    // ``exe_basename`` is the launched executable's file name (e.g.
    // "notepad.exe"), used for the alias/packaged fallback match
    // described above. May be empty (PID-only matching then).
    AppWindowWatcher(vm::AgentRpcClient& rpc, quint32 pid, QString exe_basename,
                     QObject* parent = nullptr);

    void start();
    void stop();

    [[nodiscard]] quint32 pid() const noexcept { return pid_; }

Q_SIGNALS:
    // Fires when the watcher has shut itself down (no more views,
    // grace period elapsed). Owner can drop the pointer.
    void exhausted(quint32 pid);

private Q_SLOTS:
    void on_tick();

private:
    // ``relative_to`` (may be null) is the mirror of this window's
    // guest owner; ``rel_guest_px`` is this window's client origin
    // minus the owner's, in guest pixels, so the popup can be placed
    // where the app put it relative to its parent.
    void spawn_view(std::uint64_t hwnd, const QString& title,
                    WindowView* relative_to, QPoint rel_guest_px);

    vm::AgentRpcClient& rpc_;
    quint32 pid_;
    QString exe_basename_;  // lowercased; empty = PID-only matching
    QTimer timer_;
    bool tick_in_flight_ = false;

    // HWNDs present at the first tick. Windows in this set predate the
    // launch and are excluded from the exe-name fallback so it can't
    // adopt an instance the user already had open. Empty until the
    // first tick completes (``baseline_captured_``).
    QSet<quint64> baseline_hwnds_;
    bool baseline_captured_ = false;

    // Once the exe-name fallback first identifies this launch's real
    // window, we record its owning PID and from then on match ONLY
    // that PID — exactly like a normal Win32 launch. Without this lock
    // every still-alive watcher for the same exe keeps name-adopting
    // every other instance's windows (and never idles out, because it
    // keeps "seeing" matches), so closing+reopening an app spawned
    // ever more duplicate mirrors.
    bool locked_ = false;
    quint32 locked_pid_ = 0;

    // Map persists entries even after the WindowView is destroyed
    // (QPointer goes null) — that's how we avoid respawning a mirror
    // for a window the user just told us to close while the guest
    // hasn't yet finished tearing it down.
    QHash<quint64, QPointer<WindowView>> views_;
    qint64 last_seen_unix_ = 0;
};

}  // namespace lsw::manager::ui::manager
