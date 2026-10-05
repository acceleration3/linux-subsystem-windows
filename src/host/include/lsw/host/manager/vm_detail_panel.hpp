#pragma once

#include "lsw/host/manager/app_list.hpp"
#include "lsw/host/manager/stats_widget.hpp"
#include "lsw/host/ui/vnc_view.hpp"
#include <QUuid>
#include <QWidget>

class QLabel;

namespace lsw::host::services { class HostServices; }

namespace lsw::host::manager {

// The Overview page: the preview, the four gauges, and the grid of
// applications found inside the guest.
//
// It used to carry a header of its own with a status badge and four
// buttons. Those moved to the NavRail, which is the one place in the
// window that knows where you are and what the machine is doing. What
// is left here is the content of one page.
//
// It polls nothing: the machine's state and figures are the status
// hub's (HostServices::status), read when the hub says they changed.
// The two transitions the window cares about — the domain coming up or
// going down, the agent starting or stopping answering — are reported
// upward as signals, so the rail and the status bar follow them without
// a reading of their own.
class VmDetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit VmDetailPanel(services::HostServices& services, QWidget* parent = nullptr);

    // A null uuid releases the current one.
    void set_target(const QUuid& vm_uuid);

    [[nodiscard]] bool is_running() const noexcept { return last_running_; }
    [[nodiscard]] bool is_agent_online() const noexcept { return last_agent_reachable_; }


public Q_SLOTS:
    // Driven by the rail's single power button. False when libvirt
    // would not start it.
    bool start_guest();
    void stop_guest();

Q_SIGNALS:
    // libvirt says the domain is up. Tracks the qemu process, so the
    // power button follows this one.
    void running_changed(bool running);
    // The agent answers, which is the point at which the guest is
    // actually usable. The badge follows this one, because libvirt
    // reports "running" a minute or two before a guest is interactive.
    void agent_online_changed(bool online);

private:
    // The hub has a new reading of the machine, or of its graphics (the
    // VRAM gauge is the display GPU's).
    void on_status();
    // The preview and its tag, for a machine that is (or is not)
    // running. A running one gets a paused start.
    void follow_preview(bool running);
    // The tag for a preview with a port: its address, and whether it
    // is paused.
    void tag_preview();

private:
    // Hoisted up to App so the wizard and MCP server share the same
    // channel and log buffer with the manager UI.
    services::HostServices& services_;

    QUuid uuid_;
    bool last_running_ = false;
    bool last_agent_reachable_ = false;

    lsw::host::ui::VncView* preview_ = nullptr;
    QLabel* preview_tag_ = nullptr;
    StatsWidget* stats_ = nullptr;
    AppListWidget* app_list_ = nullptr;
};

}  // namespace lsw::host::manager
