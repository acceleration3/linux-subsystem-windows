#include "lsw/host/services/status_hub.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/lifecycle.hpp"
#include "lsw/host/vm/shmem_region.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

constexpr int kGraphicsPollMs = 2000;
// A logon or a logoff is rare and slow; a five-second-old session list
// is fresh enough to pick one from.
constexpr int kSessionsPollMs = 5000;
// The device's modes change only when somebody sets them, which goes
// through here anyway; read them now and then in case it was not us.
constexpr int kModesEveryTicks = 5;

struct HostAnswer {
    QVector<platform::HostGpu> gpus;
    QVector<platform::HostApi> apis;
};

}  // namespace

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

    // Which GPUs are passed is the definition's to say. The settings
    // only decide what is pending, and with no domain defined at all
    // they stand in for it.
    connect(&manager_settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kHostGpu || key == K::kRenderGpus || key == K::kHostWindowMib
            || key == K::kIsolateRenderer) {
            rematch();
            read_vram();
            Q_EMIT changed(Area::Graphics);
        }
    });
    probe_host();
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
    renderer_.reset();
    graphics_.guest = GuestGpuStatus{};
    graphics_.live_modes.reset();
    graphics_timer_.stop();
    if (!uuid.isNull()) {
        monitor_.watch(uuid);
    }
    read_definition();
    Q_EMIT changed(Area::Vm);
    Q_EMIT changed(Area::Graphics);
    // The first reading now, so nothing shows a running machine as off
    // until the monitor's first tick.
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
    if (!definition_read_ || stats.running != definition_live_) {
        read_definition();
    }
    read_transport();
    if (!stats.running) {
        boot_clock_.invalidate();
    } else if (stats.agent_reachable && !was_online && boot_clock_.isValid()) {
        // The boot this manager started has reached an answering agent.
        manager_settings_.set_value(K::kLastBootMs, boot_clock_.elapsed());
        boot_clock_.invalidate();
    }
    // The host's own VRAM counter, at the machine's pace: it is on the
    // overview beside the guest's figures.
    read_vram();
    Q_EMIT changed(Area::Vm);

    if (stats.running != was_running) {
        if (stats.running) {
            // A boot is when the device line is read, so the host's
            // answer is worth having again.
            graphics_ticks_ = 0;
            graphics_timer_.start();
            probe_host();
            tick_graphics();
        } else {
            graphics_timer_.stop();
            graphics_.guest = GuestGpuStatus{};
            graphics_.live_modes.reset();
            // The log once more, for the last lines QEMU wrote.
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
        rematch();
        Q_EMIT changed(Area::Graphics);
        sessions_ = GuestSessions{};
        Q_EMIT changed(Area::Sessions);
    }
}

void StatusHub::refresh(Area area) {
    switch (area) {
    case Area::Vm:
        // The figures: the monitor's next tick is at most two seconds
        // away, and it is the only thing that asks libvirt and the agent
        // for them. The definition is read here, because a redefine is
        // the one change to it that is neither a start nor a stop.
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
    }
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
        // Live while it runs; while it is off, the stored one, which is
        // what the next start boots.
        const QString xml = lifecycle_.domain_xml(vm_.uuid, /*stored=*/!definition_live_);
        auto parsed = xml.isEmpty() ? std::nullopt : vm::DomainDescription::parse(xml);
        if (parsed) {
            vm_.disk_bytes = vm::DiskImage::virtual_size(parsed->disk_path);
            (definition_live_ ? vm_.running_device : vm_.next_boot) = std::move(parsed);
        }
    }
    graphics_.renderer_available = !platform::AccelVirtPaths::renderer().isEmpty();
    rematch();
}

void StatusHub::read_transport() {
    const vm::ShmemRegion* region =
        vm_.stats.running ? lifecycle_.shmem_region(vm_.uuid) : nullptr;
    vm_.region_bytes = region != nullptr ? region->size() : 0;
    vm_.doorbells = region != nullptr && lifecycle_.ivshmem_server(vm_.uuid) != nullptr;
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
    if (area != Area::Graphics) {
        return;
    }
    graphics_detail_ = on;
    if (on && vm_.running()) {
        tick_graphics();
        ask_guest();
    }
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
    if (graphics_detail_ || graphics_ticks_ % kModesEveryTicks == 1) {
        read_modes();
    }
    if (changed_now) {
        Q_EMIT changed(Area::Graphics);
    }

    // /proc is read on a worker: a few hundred small files, but files.
    if (scanning_) {
        return;
    }
    scanning_ = true;
    (void)QtConcurrent::run([uuid = vm_.uuid, known = renderer_.qemu_pid()] {
        return vm::RendererWatcher::scan(uuid, known);
    }).then(this, [this, uuid = vm_.uuid](const vm::RendererWatcher::Sample& sample) {
        scanning_ = false;
        if (uuid != vm_.uuid) {
            return;
        }
        const qint64 before = graphics_.renderer_pid;
        renderer_.update(sample);
        graphics_.renderer_pid = renderer_.renderer_pid();
        graphics_.renderer_restarts = renderer_.restarts();
        graphics_.last_renderer_restart = renderer_.last_restart();
        if (graphics_.renderer_pid != before) {
            Q_EMIT changed(Area::Graphics);
        }
    });
}

void StatusHub::probe_host() {
    if (probing_) {
        return;
    }
    probing_ = true;
    // A Vulkan instance and an EGL display each load a driver: a worker's
    // job, not the GUI thread's.
    (void)QtConcurrent::run([] {
        HostAnswer answer;
        answer.gpus = platform::HostGpus::enumerate();
        answer.apis =
            platform::HostGraphicsProbe::probe(answer.gpus, platform::AccelVirtPaths::d3d_lib_dir());
        return answer;
    }).then(this, [this](const HostAnswer& answer) {
        probing_ = false;
        graphics_.host_gpus = answer.gpus;
        graphics_.apis = answer.apis;
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
                           // A refused request is "unknown", not "no
                           // adapters".
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
                           // A refused request is "unknown", and the
                           // console falls back to the agent's own
                           // default.
                           sessions_ = reply.ok ? GuestSessions::from_json(reply.body)
                                                : GuestSessions{};
                           Q_EMIT changed(Area::Sessions);
                       });
}

void StatusHub::read_modes() {
    if (!vm_.running()) {
        return;
    }
    // A QMP command through libvirt, which is thread-affine and local:
    // a round trip on the socket QEMU already has open.
    const auto modes = lifecycle_.display_modes(vm_.uuid, nullptr);
    if (modes != graphics_.live_modes) {
        graphics_.live_modes = modes;
        Q_EMIT changed(Area::Graphics);
    }
}

void StatusHub::read_vram() {
    const QString address =
        graphics_.adapters.isEmpty() ? QString{} : graphics_.adapters.first().gpu.pci_address;
    graphics_.vram = platform::GpuMemoryUsage::read(address);
}

void StatusHub::rematch() {
    // The GPUs the machine passes, display first: from its definition's
    // virtio-gpu-av-pci lines when there is one, from the settings only
    // when there is no domain to read.
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
                // A GPU the definition names and the host no longer has:
                // still the machine's, so still listed, by what is known.
                platform::HostGpu missing;
                missing.uuid = device.host_gpu;
                missing.name = QStringLiteral("GPU %1, not on this host").arg(device.host_gpu.left(8));
                add(missing, device.display);
            }
        }
        // The display first, whatever order the lines came in.
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

    // Windows' adapters, matched to the GPUs they are: by vendor and
    // device ID first (what the device presents), by name for what is
    // left (two identical cards, or llvmpipe on virtio-gpu's own IDs),
    // and in order for whatever is left after that.
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
        // Isolation a restart can give: without av-renderer installed the
        // definition gets the renderer in QEMU whatever the setting says.
        const bool isolate =
            manager_settings_.bool_value(K::kIsolateRenderer) && graphics_.renderer_available;
        if (isolate != display->isolated()) {
            pending.isolate_renderer = isolate;
        }
    }
    // The GPUs, by the UUIDs they resolve to: "auto" in one and the first
    // GPU's UUID in the other are the same GPU. Needs the host's list.
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
            // The same filter the definition is built with: once each, not
            // the display, and only GPUs the host has.
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

}  // namespace lsw::host::services
