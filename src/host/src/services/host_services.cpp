#include "lsw/host/services/host_services.hpp"

#include "lsw/base/log.hpp"

#include <QDir>

namespace lsw::host::services {

HostServices::HostServices(QObject* parent)
    : QObject(parent), settings_(this), vms_(this), libvirt_(this),
      lifecycle_(libvirt_, this), monitor_(libvirt_, this), rpc_(this),
      windows_(rpc_, this), guest_probe_(rpc_),
      app_configs_(QDir(settings_.data_dir()).filePath("apps"), this),
      log_store_(this),
      vulkan_capture_(lifecycle_) {
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

}  // namespace lsw::host::services
