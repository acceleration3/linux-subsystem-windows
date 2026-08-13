#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>

namespace lsw::host::link {

class AgentRpcClient;

// One guest top-level window, as the agent reports it.
struct GuestWindow {
    quint64 hwnd = 0;
    quint64 owner = 0;  // GW_OWNER hwnd (0 = none) — for popup parenting
    quint32 pid = 0;
    QString title;
    QString exe_path;
    int screen_x = 0;  // client-area origin in the guest's screen space
    int screen_y = 0;
};

// The manager's single view of what windows exist in the guest.
//
// Window enumeration used to be polled per consumer: every
// ``AppWindowWatcher`` ran its own ~45 Hz ``list_windows`` timer, and the
// MCP tool and the UI each asked separately. Two apps open meant two
// pollers asking the same question at the same rate, and each of them
// separately subscribed to the same UIA push events.
//
// This is that question, asked once. Consumers connect to ``updated``
// and apply their own selection policy to the shared snapshot.
//
// The poll only runs while someone is subscribed: watchers call
// ``subscribe`` / ``unsubscribe`` around their lifetime, so an idle
// manager makes no window-enumeration traffic at all.
class GuestWindowModel : public QObject {
    Q_OBJECT
public:
    // Safety-net poll interval. The primary signal is the agent's UIA
    // push events, which arrive within a transport tick; the timer
    // covers what UIA misses (App Execution Aliases surfacing windows
    // under another PID, full-screen games that never register, the gap
    // between a launch and its first event handler).
    static constexpr int kPollMs = 22;

    explicit GuestWindowModel(AgentRpcClient& rpc, QObject* parent = nullptr);
    ~GuestWindowModel() override;

    // Reference-counted interest. Polling starts on the first
    // subscriber and stops after the last one leaves.
    void subscribe();
    void unsubscribe();

    // The most recent enumeration. Empty until the first poll returns.
    [[nodiscard]] QList<GuestWindow> snapshot() const { return latest_; }

    // Request an out-of-band refresh (a launch just happened, a UIA
    // event fired). Coalesced against the in-flight poll.
    void refresh_now();

Q_SIGNALS:
    // A fresh enumeration arrived. Emitted on the GUI thread.
    void updated(const QList<lsw::host::link::GuestWindow>& windows);

private Q_SLOTS:
    void poll();

private:
    AgentRpcClient& rpc_;
    QTimer timer_;
    bool poll_in_flight_ = false;
    int subscribers_ = 0;
    QList<GuestWindow> latest_;

    // UIA lifecycle push tokens — registered once for the whole
    // process rather than once per watcher.
    std::uint64_t opened_token_ = 0;
    std::uint64_t closed_token_ = 0;
};

}  // namespace lsw::host::link

Q_DECLARE_METATYPE(QList<lsw::host::link::GuestWindow>)
