#include "lsw/host/services/status_hub.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/platform/av_state_dump.hpp"
#include "lsw/host/platform/host_monitors.hpp"
#include "lsw/host/platform/host_usb_devices.hpp"
#include "lsw/host/platform/qemu_process.hpp"
#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QtConcurrent/QtConcurrentRun>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

constexpr int kGraphicsPollMs = 2000;
constexpr int kSessionsPollMs = 5000;
constexpr int kWindowFastMs = 1000;
constexpr int kWindowSlowMs = 5000;
constexpr int kModesEveryTicks = 5;
constexpr int kUsbPollMs = 2000;
constexpr int kMonitorsSettleMs = 250;

struct HostAnswer {
    QVector<platform::HostGpu> gpus;
    QVector<platform::HostApi> apis;
    QVector<platform::HostMonitor> monitors;
};

}

StatusHub::StatusHub(vm::LibvirtConnection& libvirt, vm::Lifecycle& lifecycle,
                     link::AgentRpcClient& rpc, vm::IGuestStatsProbe& guest_probe,
                     const config::Settings& settings,
                     config::ManagerSettings& manager_settings, QObject* parent)
    : QObject(parent),
      lifecycle_(lifecycle),
      rpc_(rpc),
      manager_settings_(manager_settings),
      monitor_(libvirt, this),
      log_(settings.vm_avgpu_log_path()) {
    monitor_.set_guest_probe(&guest_probe);
    connect(&monitor_, &vm::Monitor::stats_updated, this, &StatusHub::on_stats);
    graphics_.log_path = log_.path();

    graphics_timer_.setInterval(kGraphicsPollMs);
    connect(&graphics_timer_, &QTimer::timeout, this, &StatusHub::tick_graphics);
    sessions_timer_.setInterval(kSessionsPollMs);
    connect(&sessions_timer_, &QTimer::timeout, this, &StatusHub::ask_sessions);
    clock_.start();
    window_timer_.setInterval(kWindowSlowMs);
    connect(&window_timer_, &QTimer::timeout, this, &StatusHub::sample_window);
    usb_timer_.setInterval(kUsbPollMs);
    connect(&usb_timer_, &QTimer::timeout, this, &StatusHub::read_usb);

    connect(&manager_settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kHostGpu || key == K::kRenderGpus || key == K::kHostWindowMib) {
            rematch();
            read_vram();
            Q_EMIT changed(Area::Graphics);
        } else if (key == K::kUsbAlways && usb_.read) {
            rebuild_usb();
        }
    });
    monitors_timer_.setSingleShot(true);
    monitors_timer_.setInterval(kMonitorsSettleMs);
    connect(&monitors_timer_, &QTimer::timeout, this, &StatusHub::read_monitors);
    for (QScreen* screen : QGuiApplication::screens()) {
        watch_screen(screen);
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        watch_screen(screen);
        monitors_timer_.start();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { monitors_timer_.start(); });
    probe_host();
}

void StatusHub::watch_screen(QScreen* screen) {
    const auto again = [this] { monitors_timer_.start(); };
    connect(screen, &QScreen::geometryChanged, this, again);
    connect(screen, &QScreen::refreshRateChanged, this, again);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, again);
    connect(screen, &QScreen::physicalSizeChanged, this, again);
}

bool StatusHub::apply_monitors(const QVector<platform::HostMonitor>& monitors) {
    const bool differs = monitors != graphics_.host_monitors;
    drm_monitors_ = monitors;
    graphics_.host_monitors = monitors;
    rebuild_monitor_rates();
    return differs;
}

void StatusHub::read_monitors() {
    if (monitors_reading_) {
        monitors_again_ = true;
        return;
    }
    monitors_reading_ = true;
    (void)QtConcurrent::run([] { return platform::HostMonitors::read(); })
        .then(this, [this](const QVector<platform::HostMonitor>& monitors) {
            monitors_reading_ = false;
            const bool differs = apply_monitors(monitors);
            if (monitors_again_) {
                monitors_again_ = false;
                monitors_timer_.start();
            }
            if (differs) {
                Q_EMIT changed(Area::Graphics);
            }
        });
}

void StatusHub::set_target(const QUuid& uuid) {
    if (uuid == vm_.uuid) {
        return;
    }
    if (!vm_.uuid.isNull()) {
        monitor_.unwatch(vm_.uuid);
    }
    vm_ = VmStatus{};
    vm_.uuid = uuid;
    definition_read_ = false;
    boot_clock_.invalidate();
    graphics_.guest = GuestGpuStatus{};
    graphics_.live_modes.reset();
    graphics_.live_refresh.reset();
    graphics_.scale_percent.reset();
    graphics_timer_.stop();
    window_timer_.stop();
    window_.clear();
    usb_ = UsbStatus{};
    update_usb_timer();
    if (!uuid.isNull()) {
        monitor_.watch(uuid);
    }
    read_definition();
    Q_EMIT changed(Area::Vm);
    Q_EMIT changed(Area::Graphics);
    if (!uuid.isNull()) {
        monitor_.poll_now(uuid);
    }
}

void StatusHub::on_stats(const QUuid& uuid, const vm::VmStats& stats) {
    if (uuid != vm_.uuid) {
        return;
    }
    const bool was_running = vm_.stats.running;
    const bool was_online = vm_.stats.agent_reachable;
    vm_.stats = stats;
    if (stats.running != was_running) {
        LSW_INFO("status: the VM is {}", stats.running ? "running" : "stopped");
    }
    if (stats.agent_reachable != was_online) {
        LSW_INFO("status: the agent is {}", stats.agent_reachable ? "online" : "offline");
    }
    const int previous_boot = vm_.domain_id;
    vm_.domain_id = stats.running ? lifecycle_.domain_id(uuid) : -1;
    if (vm_.domain_id != previous_boot && previous_boot != -1 && vm_.domain_id != -1) {
        definition_read_ = false;
        usb_live_.clear();
        read_usb();
    }
    if (!definition_read_ || stats.running != definition_live_) {
        read_definition();
    }
    if (!stats.running) {
        boot_clock_.invalidate();
    } else if (stats.agent_reachable && !was_online && boot_clock_.isValid()) {
        manager_settings_.set_value(K::kLastBootMs, boot_clock_.elapsed());
        boot_clock_.invalidate();
    }
    read_vram();
    Q_EMIT changed(Area::Vm);

    if (stats.running != was_running) {
        update_usb_timer();
        read_usb();
        if (stats.running) {
            graphics_ticks_ = 0;
            graphics_timer_.start();
            window_timer_.start();
            sample_window();
            probe_host();
            tick_graphics();
        } else {
            graphics_timer_.stop();
            window_timer_.stop();
            window_.clear();
            Q_EMIT changed(Area::HostWindow);
            graphics_.guest = GuestGpuStatus{};
            graphics_.live_modes.reset();
            graphics_.live_refresh.reset();
            graphics_.scale_percent.reset();
            if (log_.poll()) {
                graphics_.log_lines = log_.lines();
                graphics_.log_line_count = log_.line_count();
                graphics_.log_facts = log_.facts();
            }
            rematch();
        }
        Q_EMIT changed(Area::Graphics);
    }
    if (stats.agent_reachable && !was_online) {
        ask_guest();
        ask_sessions();
    } else if (!stats.agent_reachable && was_online) {
        graphics_.guest = GuestGpuStatus{};
        graphics_.scale_percent.reset();
        rematch();
        Q_EMIT changed(Area::Graphics);
        sessions_ = GuestSessions{};
        Q_EMIT changed(Area::Sessions);
    }
}

void StatusHub::refresh(Area area) {
    switch (area) {
    case Area::Vm:
        read_definition();
        Q_EMIT changed(Area::Vm);
        Q_EMIT changed(Area::Graphics);
        break;
    case Area::Host:
        probe_host();
        break;
    case Area::Graphics:
        ask_guest();
        read_modes();
        break;
    case Area::Sessions:
        ask_sessions();
        break;
    case Area::Usb:
        read_usb();
        break;
    case Area::Update:
        break;
    case Area::HostWindow:
        sample_window();
        break;
    }
}

void StatusHub::set_vm(const VmStatus& status) {
    vm_ = status;
    Q_EMIT changed(Area::Vm);
}

void StatusHub::set_update(const GuestUpdateStatus& status) {
    if (status == update_) {
        return;
    }
    update_ = status;
    Q_EMIT changed(Area::Update);
}

void StatusHub::set_clipboard(const ClipboardStatus& status) {
    if (status == clipboard_) {
        return;
    }
    clipboard_ = status;
    Q_EMIT changed(Area::Host);
}

void StatusHub::boot_started(const QUuid& uuid) {
    if (uuid == vm_.uuid && !uuid.isNull()) {
        boot_clock_.start();
    }
}

std::optional<qint64> StatusHub::last_boot_ms() const {
    const qint64 ms = manager_settings_.value(K::kLastBootMs).toLongLong();
    return ms > 0 ? std::optional<qint64>{ms} : std::nullopt;
}

void StatusHub::read_definition() {
    vm_.running_device.reset();
    vm_.next_boot.reset();
    vm_.disk_bytes.reset();
    definition_read_ = true;
    definition_live_ = vm_.stats.running;
    if (!vm_.uuid.isNull()) {
        const QString xml = lifecycle_.domain_xml(vm_.uuid, !definition_live_);
        auto parsed = xml.isEmpty() ? std::nullopt : vm::DomainDescription::parse(xml);
        if (parsed) {
            vm_.disk_bytes = vm::DiskImage::virtual_size(parsed->disk_path);
            (definition_live_ ? vm_.running_device : vm_.next_boot) = std::move(parsed);
        }
    }
    rematch();
}

void StatusHub::set_detail(Area area, bool on) {
    if (area == Area::Sessions) {
        if (on) {
            ask_sessions();
            sessions_timer_.start();
        } else {
            sessions_timer_.stop();
        }
        return;
    }
    if (area == Area::Usb) {
        usb_detail_ = on;
        update_usb_timer();
        if (on) {
            read_usb();
        }
        return;
    }
    if (area != Area::Graphics) {
        return;
    }
    graphics_detail_ += on ? 1 : -1;
    update_window_timer();
    if (on && vm_.running()) {
        tick_graphics();
        ask_guest();
        sample_window();
    }
}

void StatusHub::update_window_timer() {
    window_timer_.setInterval(graphics_detail_ > 0 ? kWindowFastMs : kWindowSlowMs);
}

void StatusHub::sample_window() {
    if (!vm_.running() || window_reading_) {
        return;
    }
    window_reading_ = true;
    const QUuid uuid = vm_.uuid;
    (void)QtConcurrent::run([this, uuid] {
        return platform::AvStateDump::read(platform::QemuProcess::pid_of(uuid));
    }).then(this, [this, uuid](const platform::AvStateDump::Snapshot& snapshot) {
        window_reading_ = false;
        if (!vm_.running() || vm_.uuid != uuid) {
            return;
        }
        window_.add(snapshot, clock_.elapsed());
        Q_EMIT changed(Area::HostWindow);
    });
}

void StatusHub::tick_graphics() {
    if (!vm_.running()) {
        return;
    }
    ++graphics_ticks_;
    bool changed_now = false;
    if (log_.poll()) {
        graphics_.log_lines = log_.lines();
        graphics_.log_line_count = log_.line_count();
        graphics_.log_facts = log_.facts();
        changed_now = true;
    }
    if (graphics_detail_ > 0 || graphics_ticks_ % kModesEveryTicks == 1) {
        read_modes();
    }
    if (changed_now) {
        Q_EMIT changed(Area::Graphics);
    }
}

void StatusHub::probe_host() {
    if (probing_) {
        return;
    }
    probing_ = true;
    (void)QtConcurrent::run([] {
        HostAnswer answer;
        answer.gpus = platform::HostGpus::enumerate();
        answer.apis =
            platform::HostGraphicsProbe::probe(answer.gpus, platform::AccelVirtPaths::d3d_lib_dir());
        answer.monitors = platform::HostMonitors::read();
        return answer;
    }).then(this, [this](const HostAnswer& answer) {
        probing_ = false;
        if (graphics_.probed) {
            for (const auto& gpu : answer.gpus) {
                const bool known = std::any_of(graphics_.host_gpus.begin(), graphics_.host_gpus.end(),
                                               [&](const platform::HostGpu& old) { return old.uuid == gpu.uuid; });
                if (!known) {
                    LSW_INFO("status: host GPU appeared: {}", gpu.name.toStdString());
                }
            }
            for (const auto& old : graphics_.host_gpus) {
                const bool kept = std::any_of(answer.gpus.begin(), answer.gpus.end(),
                                              [&](const platform::HostGpu& gpu) { return old.uuid == gpu.uuid; });
                if (!kept) {
                    LSW_INFO("status: host GPU gone: {}", old.name.toStdString());
                }
            }
        } else {
            LSW_DEBUG("status: {} host GPUs, {} graphics APIs", answer.gpus.size(), answer.apis.size());
        }
        graphics_.host_gpus = answer.gpus;
        graphics_.apis = answer.apis;
        apply_monitors(answer.monitors);
        graphics_.probed = true;
        rematch();
        read_vram();
        Q_EMIT changed(Area::Graphics);
    });
}

void StatusHub::ask_guest() {
    if (!vm_.agent_online()) {
        return;
    }
    rpc_.request_async(linkproto::cmd::kGpuStatus, nlohmann::json::object(), this,
                       [this](const link::AgentReply& reply) {
                           graphics_.guest = reply.ok ? GuestGpuStatus::from_json(reply.body)
                                                      : GuestGpuStatus{};
                           rematch();
                           Q_EMIT changed(Area::Graphics);
                       });
}

void StatusHub::ask_sessions() {
    if (!vm_.agent_online()) {
        return;
    }
    rpc_.request_async(linkproto::cmd::kListSessions, nlohmann::json::object(), this,
                       [this](const link::AgentReply& reply) {
                           sessions_ = reply.ok ? GuestSessions::from_json(reply.body)
                                                : GuestSessions{};
                           Q_EMIT changed(Area::Sessions);
                       });
}

void StatusHub::read_modes() {
    if (!vm_.running()) {
        return;
    }
    const auto modes = lifecycle_.display_modes(vm_.uuid, nullptr);
    const auto refresh = lifecycle_.display_refresh(vm_.uuid, nullptr);
    if (modes != graphics_.live_modes || refresh != graphics_.live_refresh) {
        graphics_.live_modes = modes;
        graphics_.live_refresh = refresh;
        Q_EMIT changed(Area::Graphics);
    }
}

void StatusHub::set_monitor_refresh(std::optional<quint32> millihz) {
    if (millihz == graphics_.monitor_refresh_millihz) {
        return;
    }
    graphics_.monitor_refresh_millihz = millihz;
    rebuild_monitor_rates();
    Q_EMIT changed(Area::Graphics);
}

void StatusHub::set_scale_percent(std::optional<int> percent) {
    if (percent == graphics_.scale_percent) {
        return;
    }
    graphics_.scale_percent = percent;
    Q_EMIT changed(Area::Graphics);
}

void StatusHub::rebuild_monitor_rates() {
    QVector<platform::HostMonitor> screens;
    for (const QScreen* screen : QGuiApplication::screens()) {
        if (const auto rate = config::DisplayRefresh::from_hertz(screen->refreshRate())) {
            screens.push_back({screen->name(), {static_cast<qint64>(*rate)}});
        }
    }
    graphics_.monitor_rates = platform::HostMonitors::merge(drm_monitors_, screens);
}

void StatusHub::update_usb_timer() {
    if (vm_.running() || usb_detail_) {
        if (!usb_timer_.isActive()) {
            usb_timer_.start();
        }
    } else {
        usb_timer_.stop();
    }
}

void StatusHub::read_usb() {
    read_usb_host();
    read_usb_live();
}

void StatusHub::read_usb_host() {
    if (usb_reading_) {
        return;
    }
    usb_reading_ = true;
    (void)QtConcurrent::run([] { return platform::HostUsbDevices::enumerate(); })
        .then(this, [this](const QVector<platform::HostUsbDevice>& devices) {
            usb_reading_ = false;
            usb_host_ = devices;
            for (const auto& device : devices) {
                usb_names_.insert(device.identity(), device.name);
            }
            rebuild_usb();
        });
}

void StatusHub::read_usb_live() {
    if (!vm_.running()) {
        usb_live_.clear();
        if (usb_.read) {
            rebuild_usb();
        }
        return;
    }
    if (usb_live_reading_) {
        usb_live_again_ = true;
        return;
    }
    usb_live_reading_ = true;
    const QUuid uuid = vm_.uuid;
    (void)QtConcurrent::run([this, uuid] {
        std::optional<QVector<vm::DomainDescription::UsbDevice>> live;
        const QString xml = lifecycle_.domain_xml(uuid);
        if (const auto parsed = xml.isEmpty() ? std::nullopt : vm::DomainDescription::parse(xml)) {
            live = parsed->usb_devices;
        }
        return live;
    }).then(this, [this](const std::optional<QVector<vm::DomainDescription::UsbDevice>>& live) {
        usb_live_reading_ = false;
        if (!vm_.running()) {
            usb_live_.clear();
        } else if (live) {
            usb_live_ = *live;
        }
        if (usb_.read) {
            rebuild_usb();
        }
        if (usb_live_again_) {
            usb_live_again_ = false;
            read_usb_live();
        }
    });
}

void StatusHub::rebuild_usb() {
    UsbStatus next = UsbStatus::build(usb_host_, vm_.running() ? usb_live_ : QVector<vm::DomainDescription::UsbDevice>{},
                                      manager_settings_.string_list_value(K::kUsbAlways), vm_.running(),
                                      vm_.domain_id, usb_names_);
    if (usb_.read && next == usb_) {
        return;
    }
    if (usb_.read) {
        for (const auto& device : next.devices) {
            const auto* before = usb_.find(device.identity, device.bus, device.device);
            if (before == nullptr) {
                LSW_INFO("status: USB device appeared: {} ({:04x}:{:04x})", device.name.toStdString(),
                         device.vendor, device.product);
            } else if (before->place != device.place) {
                LSW_DEBUG("status: USB device {} is now {}", device.name.toStdString(),
                         device.place == UsbStatus::Place::Guest  ? "in the guest"
                         : device.place == UsbStatus::Place::Host ? "on the host"
                                                                  : "absent");
            }
        }
        for (const auto& device : usb_.devices) {
            if (next.find(device.identity, device.bus, device.device) == nullptr) {
                LSW_INFO("status: USB device gone: {}", device.name.toStdString());
            }
        }
    }
    LSW_DEBUG("status: USB snapshot has {} devices", next.devices.size());
    usb_ = std::move(next);
    Q_EMIT changed(Area::Usb);
}

void StatusHub::read_vram() {
    const QString address =
        graphics_.adapters.isEmpty() ? QString{} : graphics_.adapters.first().gpu.pci_address;
    graphics_.vram = platform::GpuMemoryUsage::read(address);
}

void StatusHub::rematch() {
    const auto& gpus = graphics_.host_gpus;
    QVector<GraphicsAdapter> out;
    const auto add = [&out](const platform::HostGpu& gpu, bool display) {
        const bool seen = std::any_of(out.begin(), out.end(), [&](const GraphicsAdapter& a) {
            return a.gpu.uuid == gpu.uuid;
        });
        if (!seen) {
            out.push_back(GraphicsAdapter{gpu, display, std::nullopt});
        }
    };
    if (const vm::DomainDescription* definition = vm_.definition();
        definition != nullptr && !definition->gpus.isEmpty()) {
        for (const auto& device : definition->gpus) {
            if (const auto gpu = platform::HostGpus::resolve(gpus, device.host_gpu)) {
                add(*gpu, device.display);
            } else if (graphics_.probed && device.host_gpu != QLatin1StringView("auto")) {
                platform::HostGpu missing;
                missing.uuid = device.host_gpu;
                missing.name = QStringLiteral("GPU %1, not on this host").arg(device.host_gpu.left(8));
                add(missing, device.display);
            }
        }
        std::stable_partition(out.begin(), out.end(),
                              [](const GraphicsAdapter& a) { return a.display; });
    } else {
        const auto display =
            platform::HostGpus::resolve(gpus, manager_settings_.string_value(K::kHostGpu));
        if (display) {
            add(*display, true);
        }
        for (const QString& uuid : manager_settings_.string_list_value(K::kRenderGpus)) {
            const auto gpu = platform::HostGpus::resolve(gpus, uuid.trimmed());
            if (gpu && !(display && gpu->uuid == display->uuid)) {
                add(*gpu, false);
            }
        }
    }

    QVector<GuestGpuStatus::Adapter> pool = graphics_.guest.adapters;
    const auto take = [&pool](auto&& matches) -> std::optional<GuestGpuStatus::Adapter> {
        for (qsizetype i = 0; i < pool.size(); ++i) {
            if (matches(pool.at(i))) {
                return pool.takeAt(i);
            }
        }
        return std::nullopt;
    };
    for (auto& adapter : out) {
        adapter.guest = take([&](const GuestGpuStatus::Adapter& g) {
            return g.vendor_id == adapter.gpu.vendor_id && g.device_id == adapter.gpu.device_id;
        });
    }
    for (auto& adapter : out) {
        if (!adapter.guest) {
            adapter.guest = take([&](const GuestGpuStatus::Adapter& g) {
                return g.name.compare(platform::HostGpus::adapter_name(adapter.gpu),
                                      Qt::CaseInsensitive) == 0;
            });
        }
    }
    for (auto& adapter : out) {
        if (!adapter.guest && !pool.isEmpty()) {
            adapter.guest = pool.takeFirst();
        }
    }
    graphics_.adapters = out;
    compare_settings();
}

void StatusHub::compare_settings() {
    PendingGraphics pending;
    const vm::DomainDescription* definition = vm_.definition();
    const vm::DomainDescription::GpuDevice* display =
        definition != nullptr ? definition->display() : nullptr;
    if (display != nullptr) {
        const int window = manager_settings_.int_value(K::kHostWindowMib);
        if (display->hostmem_mib != 0 && window != display->hostmem_mib) {
            pending.host_window_mib = window;
        }
    }
    if (definition != nullptr && !definition->gpus.isEmpty() && graphics_.probed) {
        const auto& gpus = graphics_.host_gpus;
        const auto uuid_of = [&gpus](const QString& setting) {
            const auto gpu = platform::HostGpus::resolve(gpus, setting.trimmed());
            return gpu ? gpu->uuid : setting.trimmed().toLower();
        };
        QString defined_display;
        QStringList defined_render;
        for (const auto& device : definition->gpus) {
            if (device.display) {
                defined_display = uuid_of(device.host_gpu);
            } else {
                defined_render << uuid_of(device.host_gpu);
            }
        }
        const QString wanted_display = uuid_of(manager_settings_.string_value(K::kHostGpu));
        QStringList wanted_render;
        for (const QString& entry : manager_settings_.string_list_value(K::kRenderGpus)) {
            const auto gpu = platform::HostGpus::resolve(gpus, entry.trimmed());
            if (gpu && gpu->uuid != wanted_display && !wanted_render.contains(gpu->uuid)) {
                wanted_render << gpu->uuid;
            }
        }
        defined_render.sort();
        wanted_render.sort();
        pending.adapters = defined_display != wanted_display || defined_render != wanted_render;
    }
    graphics_.pending = pending;
}

}
