#pragma once

#include "lsw/host/services/clipboard_status.hpp"
#include "lsw/host/services/graphics_status.hpp"
#include "lsw/host/services/guest_sessions.hpp"
#include "lsw/host/services/guest_update_status.hpp"
#include "lsw/host/services/usb_status.hpp"
#include "lsw/host/services/vm_status.hpp"
#include "lsw/host/vm/avgpu_log.hpp"
#include "lsw/host/vm/monitor.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUuid>
#include <optional>

class QScreen;

namespace lsw::host::config {
class ManagerSettings;
class Settings;
}
namespace lsw::host::link { class AgentRpcClient; }
namespace lsw::host::vm {
class IGuestStatsProbe;
class LibvirtConnection;
class Lifecycle;
}

namespace lsw::host::services {
class StatusHub : public QObject {
    Q_OBJECT
public:
    enum class Area { Vm, Graphics, Host, Sessions, Usb, Update };

    StatusHub(vm::LibvirtConnection& libvirt, vm::Lifecycle& lifecycle,
              link::AgentRpcClient& rpc, vm::IGuestStatsProbe& guest_probe,
              const config::Settings& settings, config::ManagerSettings& manager_settings,
              QObject* parent = nullptr);

    void set_monitor_refresh(std::optional<quint32> millihz);

    void set_scale_percent(std::optional<int> percent);

    void set_target(const QUuid& uuid);

    [[nodiscard]] const VmStatus& vm() const noexcept { return vm_; }
    [[nodiscard]] const GraphicsStatus& graphics() const noexcept { return graphics_; }
    [[nodiscard]] const GuestSessions& sessions() const noexcept { return sessions_; }
    [[nodiscard]] const UsbStatus& usb() const noexcept { return usb_; }
    [[nodiscard]] const GuestUpdateStatus& update() const noexcept { return update_; }

    [[nodiscard]] const ClipboardStatus& clipboard() const noexcept { return clipboard_; }

    void set_vm(const VmStatus& status);

    void set_update(const GuestUpdateStatus& status);

    void set_clipboard(const ClipboardStatus& status);

    void refresh(Area area);

    void boot_started(const QUuid& uuid);

    [[nodiscard]] std::optional<qint64> last_boot_ms() const;

    void set_detail(Area area, bool on);

Q_SIGNALS:
    void changed(services::StatusHub::Area area);

private:
    void on_stats(const QUuid& uuid, const vm::VmStats& stats);
    void tick_graphics();
    void probe_host();
    void watch_screen(QScreen* screen);
    void read_monitors();
    bool apply_monitors(const QVector<platform::HostMonitor>& monitors);
    void ask_guest();
    void ask_sessions();
    void read_modes();
    void rebuild_monitor_rates();
    void read_vram();
    void read_usb();
    void read_usb_host();
    void read_usb_live();
    void rebuild_usb();
    void update_usb_timer();
    void read_definition();
    void rematch();
    void compare_settings();

    vm::Lifecycle& lifecycle_;
    link::AgentRpcClient& rpc_;
    config::ManagerSettings& manager_settings_;
    vm::Monitor monitor_;

    VmStatus vm_;
    GraphicsStatus graphics_;
    QVector<platform::HostMonitor> drm_monitors_;
    GuestSessions sessions_;
    vm::AvgpuLog log_;
    QTimer graphics_timer_;
    int graphics_ticks_ = 0;
    int graphics_detail_ = 0;
    QTimer sessions_timer_;
    bool probing_ = false;
    QTimer monitors_timer_;
    bool monitors_reading_ = false;
    bool monitors_again_ = false;
    QTimer usb_timer_;
    bool usb_detail_ = false;
    bool usb_reading_ = false;
    bool usb_live_reading_ = false;
    bool usb_live_again_ = false;
    UsbStatus usb_;
    GuestUpdateStatus update_;
    ClipboardStatus clipboard_;
    QVector<platform::HostUsbDevice> usb_host_;
    QHash<QString, QString> usb_names_;
    QVector<vm::DomainDescription::UsbDevice> usb_live_;
    bool definition_read_ = false;
    bool definition_live_ = false;
    QElapsedTimer boot_clock_;
};

}
