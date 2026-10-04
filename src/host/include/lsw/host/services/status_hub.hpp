#pragma once

#include "lsw/host/services/graphics_status.hpp"
#include "lsw/host/services/guest_sessions.hpp"
#include "lsw/host/services/vm_status.hpp"
#include "lsw/host/vm/avgpu_log.hpp"
#include "lsw/host/vm/monitor.hpp"
#include "lsw/host/vm/renderer_watcher.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUuid>
#include <optional>

namespace lsw::host::config {
class ManagerSettings;
class Settings;
}  // namespace lsw::host::config
namespace lsw::host::link { class AgentRpcClient; }
namespace lsw::host::vm {
class IGuestStatsProbe;
class LibvirtConnection;
class Lifecycle;
}  // namespace lsw::host::vm

namespace lsw::host::services {

// Every status the application shows, polled in one place.
//
// The machine (libvirt's view and the guest agent's figures, through
// vm::Monitor) and the guest's graphics (the host's GPUs and APIs, the
// device's live modes, the renderer process, the agent's gpu_status, the
// driver's log, the display GPU's VRAM) each have a snapshot here, kept
// current on the cadence that suits it. Widgets read the snapshots and
// listen for ``changed``; none of them polls anything itself.
//
// The machine's shape — cores, memory, the system disk, the GPU devices
// with their host window and renderer — is read from libvirt's own
// definition of the domain, never from the settings: the live one while
// it runs, the stored one while it is off, re-read whenever it starts or
// stops. The settings only show through as what a restart would change.
//
// Cadences: the machine every two seconds while there is a VM to watch;
// the graphics sources every two seconds while it runs, and not at all
// while it is off, apart from the host probe, which runs at start-up,
// at each boot and on demand. Probes and agent calls run off the GUI
// thread; their answers are applied on it.
class StatusHub : public QObject {
    Q_OBJECT
public:
    // What a snapshot or a refresh is about. Host is the host half of
    // the graphics snapshot — its GPUs and the APIs it serves — kept
    // apart for refresh only, because probing it loads drivers; its
    // answers are announced as a Graphics change like the rest.
    enum class Area { Vm, Graphics, Host, Sessions };

    StatusHub(vm::LibvirtConnection& libvirt, vm::Lifecycle& lifecycle,
              link::AgentRpcClient& rpc, vm::IGuestStatsProbe& guest_probe,
              const config::Settings& settings, config::ManagerSettings& manager_settings,
              QObject* parent = nullptr);

    // The VM to watch; null stops watching.
    void set_target(const QUuid& uuid);

    [[nodiscard]] const VmStatus& vm() const noexcept { return vm_; }
    [[nodiscard]] const GraphicsStatus& graphics() const noexcept { return graphics_; }
    [[nodiscard]] const GuestSessions& sessions() const noexcept { return sessions_; }

    // Ask now rather than at the next tick: Host for "Check again",
    // Graphics after a driver install or a resolution just set (the
    // agent's gpu_status and the device's modes), Vm after the domain
    // was redefined (its stored definition; the figures are read every
    // two seconds anyway).
    void refresh(Area area);

    // ``uuid`` was just started by this manager: the clock for how long a
    // boot takes runs from here to the agent's first answer.
    void boot_started(const QUuid& uuid);

    // How long the last measured boot took, start to agent; unset before
    // the first one. Kept across runs of the manager.
    [[nodiscard]] std::optional<qint64> last_boot_ms() const;

    // Somebody is looking at ``area`` closely (its page is on screen):
    // the sources that only that page shows are read on every tick
    // rather than now and then.
    void set_detail(Area area, bool on);

Q_SIGNALS:
    void changed(services::StatusHub::Area area);

private:
    void on_stats(const QUuid& uuid, const vm::VmStats& stats);
    void tick_graphics();
    void probe_host();
    void ask_guest();
    void ask_sessions();
    void read_modes();
    void read_vram();
    // The domain's definition, live or stored as the machine is, and
    // what follows from it.
    void read_definition();
    // The shared region's size and doorbells, which follow the VM.
    void read_transport();
    void rematch();
    // What the settings would change at the next restart.
    void compare_settings();

    vm::Lifecycle& lifecycle_;
    link::AgentRpcClient& rpc_;
    config::ManagerSettings& manager_settings_;
    vm::Monitor monitor_;

    VmStatus vm_;
    GraphicsStatus graphics_;
    GuestSessions sessions_;
    vm::RendererWatcher renderer_;
    vm::AvgpuLog log_;
    QTimer graphics_timer_;
    int graphics_ticks_ = 0;
    bool graphics_detail_ = false;
    // Sessions come and go with logons, so they are re-asked while a page
    // that offers them is on screen, and once when the agent arrives.
    QTimer sessions_timer_;
    bool probing_ = false;
    bool scanning_ = false;
    // Which state the definition was last read for: read again when the
    // machine starts or stops.
    bool definition_read_ = false;
    bool definition_live_ = false;
    // From boot_started to the agent's first answer; invalid otherwise.
    QElapsedTimer boot_clock_;
};

}  // namespace lsw::host::services
