#include "lsw/host/services/host_services.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/render_server.hpp"

#include <QDir>

namespace lsw::host::services {

HostServices::HostServices(QObject* parent)
    : QObject(parent), settings_(this), manager_settings_(this),
      pending_changes_(manager_settings_, this), render_stats_(this), vms_(this),
      libvirt_(this),
      lifecycle_(libvirt_, this), monitor_(libvirt_, this), rpc_(this),
      guest_probe_(rpc_),
      app_configs_(QDir(settings_.data_dir()).filePath("apps"), this),
      log_store_(this) {
    // Wire the two services that consume the agent channel. Done here,
    // not by the UI, so records and stats are collected from the moment
    // the channel connects — the manager window may never be shown at
    // all (``--launch`` opens a guest app directly).
    log_store_.attach(rpc_);
    monitor_.set_guest_probe(&guest_probe_);
    // Frame transport follows the region's lifetime, not the UI's. The
    // region exists only from VM start to force-off; every UI-side
    // binding point runs outside that window on a cold start, so they
    // all bound null and no window mirror ever received a frame.
    connect(&lifecycle_, &vm::Lifecycle::shmem_ready, this, [this](const QUuid& uuid) {
        rpc_.attach_shmem(lifecycle_.ivshmem_server(uuid), lifecycle_.shmem_region(uuid));
    });
    connect(&lifecycle_, &vm::Lifecycle::shmem_gone, this,
            [this](const QUuid&) { rpc_.detach_shmem(); });

    // The render server takes its sizes on the command line, so they
    // have to be in place before anything starts one.
    apply_render_sizing();
    connect(&manager_settings_, &config::ManagerSettings::changed, this,
            [this](const QString&) { apply_render_sizing(); });

    // Every line the server prints goes past the congestion parser. It
    // already counts stalls and already knows what size would have
    // avoided them; none of that was reaching anybody.
    connect(&lifecycle_, &vm::Lifecycle::render_server_logged, this,
            [this](const QString& line) { render_stats_.ingest(line); });
}

void HostServices::apply_render_sizing() {
    using K = config::ManagerSettings::Key;
    vm::RenderServer::Sizing sizing;
    sizing.sessions = manager_settings_.int_value(QLatin1String(K::kChannels));
    sizing.ring_kib = manager_settings_.int_value(QLatin1String(K::kRingKib));
    sizing.reply_kib = manager_settings_.int_value(QLatin1String(K::kReplyKib));
    sizing.blob_mib = manager_settings_.int_value(QLatin1String(K::kBlobMib));
    vm::RenderServer::set_sizing(sizing);

    render_stats_.set_channel_count(sizing.sessions);
    render_stats_.set_area_sizes(sizing.ring_kib, sizing.reply_kib, sizing.blob_mib);
}

HostServices::~HostServices() = default;

bool HostServices::open() {
    if (!settings_.ensure_directories()) {
        LSW_ERROR("could not create LSW data directories");
        return false;
    }
    if (libvirt_.open() != 0) {
        LSW_WARN("libvirt unavailable — VM controls disabled");
    }
    return true;
}

void HostServices::attach_running_vms() {
    // A VM the user left running when they closed the last manager. Its
    // shared region outlived that process, so take it over rather than
    // making them restart Windows to get their mirrors back.
    //
    // Separate from ``open`` because the VM list is not known until the
    // domain importer has run, which needs an open libvirt — so this is
    // the first moment there is anything to attach to.
    for (const auto& record : vms_.records()) {
        if (lifecycle_.is_running(record.uuid)) {
            (void)lifecycle_.attach_running(record.uuid);
        }
    }
}

}  // namespace lsw::host::services
