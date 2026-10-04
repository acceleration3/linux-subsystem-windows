#pragma once

#include "lsw/host/manager/app_list.hpp"
#include "lsw/host/manager/stats_widget.hpp"
#include "lsw/host/ui/vnc_view.hpp"
#include "lsw/host/vm/vm_stats.hpp"

#include <QTimer>
#include <QUuid>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace lsw::host::services { class HostServices; }

namespace lsw::host::manager {

// The Overview page: live preview, the four gauges, and the grid of
// applications found inside the guest.
//
// It used to carry a header of its own with a status badge and four
// buttons. Those moved to the NavRail, which is the one place in the
// window that knows where you are and what the machine is doing. What
// is left here is the content of one page.
//
// The panel still owns the polling, because it already holds the
// services the polling needs. It reports what
// it learns upward instead of drawing it: the rail renders the state,
// the window fills its status bar, and neither has to poll a second
// time.
class VmDetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit VmDetailPanel(services::HostServices& services, QWidget* parent = nullptr);

    // A null uuid releases the current one.
    void set_target(const QUuid& vm_uuid);

    [[nodiscard]] bool is_running() const noexcept { return last_running_; }
    [[nodiscard]] bool is_agent_online() const noexcept { return last_agent_reachable_; }

public Q_SLOTS:
    // Hand the preview to a window of its own, and take it back when
    // that window closes. One VNC connection either way: a second one
    // would be a second client on a display the guest is already
    // sharing.
    void pop_out_preview();

    // Driven by the rail's single power button.
    void start_guest();
    void stop_guest();

Q_SIGNALS:
    // libvirt says the domain is up. Tracks the qemu process, so the
    // power button follows this one.
    void running_changed(bool running);
    // The agent answers, which is the point at which the guest is
    // actually usable. The badge follows this one, because libvirt
    // reports "running" a minute or two before a guest is interactive.
    void agent_online_changed(bool online);

private Q_SLOTS:
    void on_stats_updated(const QUuid& uuid, const vm::VmStats& stats);
    void on_state_tick();

private:
    // Hoisted up to App so the wizard and MCP server share the same
    // channel and log buffer with the manager UI.
    services::HostServices& services_;

    QUuid uuid_;
    bool last_running_ = false;
    bool last_agent_reachable_ = false;
    QTimer state_timer_;

    lsw::host::ui::VncView* preview_ = nullptr;
    QVBoxLayout* preview_slot_ = nullptr;
    QLabel* preview_tag_ = nullptr;
    StatsWidget* stats_ = nullptr;
    AppListWidget* app_list_ = nullptr;
};

}  // namespace lsw::host::manager
