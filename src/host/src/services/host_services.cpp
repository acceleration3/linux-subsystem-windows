#include "lsw/host/services/host_services.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/platform/dolphin_places.hpp"
#include "lsw/host/platform/gtk_bookmarks.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"
#include "lsw/host/platform/qemu_process.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/services/domain_inputs.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"

#include <QDir>
#include <QSet>

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

constexpr std::chrono::milliseconds kShutdownSwitchTimeout{4000};

const QSet<QString>& integration_keys() {
    static const QSet<QString> keys = {
        K::kMenuCategory,  K::kPublishEntries, K::kFileAssociations, K::kSmbEnabled,  K::kGuestUser,
        K::kGuestPassword, K::kShareScope,     K::kBookmarkDolphin,  K::kBookmarkGtk,
    };
    return keys;
}

}

HostServices::HostServices(QObject* parent)
    : QObject(parent), settings_(this), manager_settings_(this),
      pending_changes_(manager_settings_, this), vms_(this), libvirt_(this),
      lifecycle_(libvirt_, this), rpc_(this), guest_probe_(rpc_),
      status_(libvirt_, lifecycle_, rpc_, guest_probe_, settings_, manager_settings_, this),
      shells_(rpc_, status_, this), app_errors_(rpc_, this),
      clipboard_(rpc_, status_, manager_settings_, this),
      guest_update_(settings_, status_, rpc_, this), upkeep_(status_, rpc_, guest_update_, this),
      usb_(lifecycle_, status_, manager_settings_, this), host_shutdown_(lifecycle_, status_, this),
      app_configs_(QDir(settings_.data_dir()).filePath("apps"), this), log_store_(this),
      vm_logs_(settings_.vm_logs_dir()), logging_(settings_, manager_settings_, log_store_, this),
      agent_log_levels_(status_, rpc_, manager_settings_, logging_.verbosity(), this),
      log_collector_(
          settings_, manager_settings_, rpc_,
          [this] {
              SystemReport::Inputs inputs;
              inputs.shipped_release = status_.update().shipped_release;
              inputs.guest_release = status_.update().guest_release;
              inputs.agent_online = status_.vm().agent_online();
              inputs.domain_xml = lifecycle_.domain_xml(status_.vm().uuid);
              return inputs;
          },
          this),
      log_cleaner_(
          settings_, rpc_, log_store_, [this] { return status_.vm().agent_online(); },
          [this] { return status_.vm().running(); }, this),
      mirrors_(rpc_, this), app_launcher_(rpc_, mirrors_, this),
      one_to_one_(manager_settings_, guest_display(), this),
      vm_scope_(QStringLiteral("lsw-vm")), mirror_scope_(QStringLiteral("lsw-mirror")),
      priority_(
          manager_settings_, vm_scope_, mirror_scope_, process_threads_,
          [this] { return platform::QemuProcess::pid_of(status_.vm().uuid); }, this) {
    log_store_.attach(rpc_);

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

    connect(&mirrors_, &mirror::GuestMirrors::desktop_opened, &one_to_one_, &OneToOneDesktop::opened);
    connect(&mirrors_, &mirror::GuestMirrors::desktop_resized, &one_to_one_, &OneToOneDesktop::resized);
    connect(&mirrors_, &mirror::GuestMirrors::desktop_closed, &one_to_one_, &OneToOneDesktop::closed);
    connect(&mirrors_, &mirror::GuestMirrors::host_started, &priority_, &VmPriority::mirror_started);
    connect(&mirrors_, &mirror::GuestMirrors::host_ended, &priority_, &VmPriority::mirror_ended);
    connect(&priority_, &VmPriority::changed, this, [this] { status_.set_priority(priority_.status()); });
    status_.set_priority(priority_.status());
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Vm) {
            priority_.vm_seen(status_.vm().running() ? status_.vm().domain_id : -1);
        }
    });
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area != StatusHub::Area::Vm || status_.vm().agent_online() == agent_online_) {
            return;
        }
        agent_online_ = status_.vm().agent_online();
        if (agent_online_) {
            one_to_one_.guest_ready();
        }
    });
}

OneToOneDesktop::Guest HostServices::guest_display() {
    const auto now = [](const link::AgentReply& reply) -> std::optional<config::DisplayMode> {
        const config::DisplayMode mode{reply.body.value("width", 0), reply.body.value("height", 0)};
        if (!reply.ok || mode.width <= 0 || mode.height <= 0) {
            return std::nullopt;
        }
        return mode;
    };
    const auto params = [](const std::optional<config::DisplayMode>& wanted) {
        return wanted ? nlohmann::json{{"width", wanted->width}, {"height", wanted->height}}
                      : nlohmann::json::object();
    };
    OneToOneDesktop::Guest guest;
    guest.offer = [this](const QString& modes) {
        const config::VmRecord* record = vms_.primary();
        if (record == nullptr || !status_.vm().running()) {
            return false;
        }
        const bool offered = lifecycle_.set_display_modes(record->uuid, modes, nullptr);
        status_.refresh(StatusHub::Area::Graphics);
        return offered;
    };
    guest.resolution = [this, now, params](const std::optional<config::DisplayMode>& wanted,
                                           OneToOneDesktop::Answer done) {
        rpc_.request_async(linkproto::cmd::kSetDisplay, params(wanted), this,
                           [now, done = std::move(done)](const link::AgentReply& reply) { done(now(reply)); });
    };
    guest.resolution_now = [this, now, params](const std::optional<config::DisplayMode>& wanted) {
        return status_.vm().agent_online()
                   ? now(rpc_.request(linkproto::cmd::kSetDisplay, params(wanted), kShutdownSwitchTimeout))
                   : std::nullopt;
    };
    return guest;
}

void HostServices::apply_integrations() {
    apps::DesktopLauncher::set_category(manager_settings_.string_value(K::kMenuCategory));
    apps::DesktopLauncher::set_publishing(manager_settings_.bool_value(K::kPublishEntries));
    apps::DesktopLauncher::set_file_associations(
        manager_settings_.bool_value(K::kFileAssociations));
    apps::DesktopLauncher::sync(app_configs_.configs());

    const bool serving = manager_settings_.bool_value(K::kSmbEnabled) && vms_.primary() != nullptr;
    const QString user = manager_settings_.string_value(K::kGuestUser);
    const QString password = manager_settings_.string_value(K::kGuestPassword);
    const QString share =
        platform::GuestSmbShare::share_for_scope(manager_settings_.string_value(K::kShareScope));

    if (serving && manager_settings_.bool_value(K::kBookmarkDolphin)) {
        (void)platform::DolphinPlaces::register_windows_bookmark(user, password, share);
    } else {
        platform::DolphinPlaces::remove_windows_bookmark();
    }
    if (serving && manager_settings_.bool_value(K::kBookmarkGtk)) {
        (void)platform::GtkBookmarks::register_windows_bookmark(user, password, share);
    } else {
        platform::GtkBookmarks::remove_windows_bookmark();
    }
}

bool HostServices::bind_agent(const QUuid& uuid) {
    const auto* record = vms_.find(uuid);
    if (record == nullptr) {
        return false;
    }
    rpc_.set_target(record->vsock_cid, linkproto::kAgentTcpPort);
    return true;
}

bool HostServices::redefine_vm(const config::VmRecord& record) {
    config::VmRecord sized = record;
    sized.sizing = vm_sizing_from_settings(manager_settings_);
    const vm::DomainXmlInputs inputs = domain_inputs_from_settings(
        settings_, manager_settings_, status_.graphics().monitor_refresh_millihz);
    if (lifecycle_.redefine(sized, inputs) != 0) {
        return false;
    }
    status_.refresh(StatusHub::Area::Vm);
    return true;
}

int HostServices::start_vm(const QUuid& uuid) {
    const auto* record = vms_.find(uuid);
    const bool cold = !lifecycle_.is_running(uuid);
    if (cold && record != nullptr && record->first_boot_done && !redefine_vm(*record)) {
        LSW_WARN("start: the definition could not be rewritten from the settings; starting the one libvirt has");
    }
    const int rc = lifecycle_.start(uuid, record != nullptr && record->first_boot_done);
    if (rc == 0 && cold) {
        status_.boot_started(uuid);
    }
    return rc;
}

HostServices::~HostServices() {
    one_to_one_.shutdown();
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
