#include "lsw/host/services/host_services.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"
#include "lsw/linkproto/envelope.hpp"

#include <QDir>
#include <QSet>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

const QSet<QString>& integration_keys() {
    static const QSet<QString> keys = {
        K::kMenuCategory,
        K::kPublishEntries,
        K::kFileAssociations,
    };
    return keys;
}

}

HostServices::HostServices(QObject* parent)
    : QObject(parent), settings_(this), manager_settings_(this),
      pending_changes_(manager_settings_, this), vms_(this), libvirt_(this),
      lifecycle_(libvirt_, this), rpc_(this), guest_probe_(rpc_),
      status_(libvirt_, lifecycle_, rpc_, guest_probe_, settings_, manager_settings_, this),
      shells_(rpc_, status_, this),
      guest_update_(settings_, status_, rpc_, this),
      upkeep_(status_, rpc_, guest_update_, this),
      usb_(lifecycle_, status_, manager_settings_, this),
      host_shutdown_(lifecycle_, status_, this),
      mount_helper_(platform::GuestSmbShare::mount_point(), this),
      indexers_(commands_, QDir::homePath()),
      windows_mount_(status_, manager_settings_, mount_helper_, platform::GuestSmbShare::mount_point(),
                     {}, [this](const QString& point) { indexers_.apply(point); }, this),
      app_configs_(QDir(settings_.data_dir()).filePath("apps"), this), log_store_(this),
      vm_logs_(settings_.vm_logs_dir()),
      logging_(settings_, manager_settings_, log_store_, this),
      agent_log_levels_(status_, rpc_, manager_settings_, logging_.verbosity(), this),
      log_collector_(settings_, manager_settings_, rpc_,
                     [this] {
                         SystemReport::Inputs inputs;
                         inputs.shipped_release = status_.update().shipped_release;
                         inputs.guest_release = status_.update().guest_release;
                         inputs.agent_online = status_.vm().agent_online();
                         inputs.domain_xml = lifecycle_.domain_xml(status_.vm().uuid);
                         return inputs;
                     },
                     this),
      log_cleaner_(settings_, rpc_, log_store_,
                   [this] { return status_.vm().agent_online(); },
                   [this] { return status_.vm().running(); }, this),
      app_launcher_(rpc_, this) {
    log_store_.attach(rpc_);
    connect(&host_shutdown_, &HostShutdownWatch::shutting_down, &windows_mount_,
            &WindowsMount::unmount_for_shutdown, Qt::DirectConnection);

    lifecycle_.set_before_start([this] { vm_logs_.prepare_start(); });
    lifecycle_.set_avgpu_log_path(settings_.vm_avgpu_log_path());
    lifecycle_.set_gpu_log_env([this] {
        const QString level = config::LogVerbosity::effective(
            manager_settings_, config::LogVerbosity::Part::Gpu);
        return vm::Lifecycle::GpuLogEnv{config::LogVerbosity::dxvk_level(level),
                                        config::LogVerbosity::vkd3d_level(level)};
    });

    connect(&manager_settings_, &config::ManagerSettings::changed, this,
            [this](const QString& key) {
                if (integration_keys().contains(key)) {
                    apply_integrations();
                }
            });
}

void HostServices::apply_integrations() {
    apps::DesktopLauncher::set_category(manager_settings_.string_value(K::kMenuCategory));
    apps::DesktopLauncher::set_publishing(manager_settings_.bool_value(K::kPublishEntries));
    apps::DesktopLauncher::set_file_associations(
        manager_settings_.bool_value(K::kFileAssociations));
    apps::DesktopLauncher::sync(app_configs_.configs());
}

bool HostServices::bind_agent(const QUuid& uuid) {
    const auto* record = vms_.find(uuid);
    if (record == nullptr) {
        return false;
    }
    rpc_.set_target(record->vsock_cid, linkproto::kAgentTcpPort);
    return true;
}

int HostServices::start_vm(const QUuid& uuid) {
    const auto* record = vms_.find(uuid);
    const bool cold = !lifecycle_.is_running(uuid);
    const int rc = lifecycle_.start(uuid, record != nullptr && record->first_boot_done);
    if (rc == 0 && cold) {
        status_.boot_started(uuid);
    }
    return rc;
}

HostServices::~HostServices() {
    windows_mount_.unmount_for_shutdown();
}

bool HostServices::open() {
    if (!settings_.ensure_directories()) {
        LSW_ERROR("could not create LSW data directories");
        return false;
    }
    logging_.start();
    vm_logs_.refresh_links();
    if (libvirt_.open() != 0) {
        LSW_WARN("libvirt unavailable; VM controls disabled");
    }
    return true;
}

}
