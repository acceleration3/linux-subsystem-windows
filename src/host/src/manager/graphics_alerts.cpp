#include "lsw/host/manager/graphics_alerts.hpp"

#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <QDir>

namespace lsw::host::manager {

namespace {

using Area = services::StatusHub::Area;

services::DriverManifest shipped_manifest() {
    return services::DriverManifest::read(
        QDir(platform::AccelVirtPaths::driver_tree()).filePath(QStringLiteral("package/package.sha256")));
}

}  // namespace

GraphicsAlerts::GraphicsAlerts(services::StatusHub& status, link::AgentRpcClient& rpc,
                               QObject* parent)
    : QObject(parent), status_(status), installer_(rpc), update_(rpc, installer_),
      shipped_(shipped_manifest()) {
    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        const bool running = status_.vm().running();
        const bool online = status_.vm().agent_online();
        if (online != agent_online_ || running != running_) {
            agent_online_ = online;
            if (!online) {
                // The job lives in the agent; its end will not come.
                installer_.abandon(QStringLiteral("the agent stopped answering before the "
                                                  "install reported its end"));
                update_.abandon(QStringLiteral("the agent stopped answering during the upload"));
            }
            if (running_ && !running) {
                // The restart the result asked for has happened, and a
                // failure belongs to the boot it was tried on: the next
                // one reads the adapters afresh.
                install_result_ = InstallResult::None;
                install_error_.clear();
            }
            running_ = running;
            Q_EMIT changed();
            return;
        }
        if (area == Area::Graphics) {
            // An LSW update replaces the package under a running manager.
            shipped_ = shipped_manifest();
            Q_EMIT changed();
        }
    });
    connect(&installer_, &GuestDriverInstaller::progress, this, [this](const QString& message) {
        install_progress_ = message;
        install_result_ = InstallResult::None;
        Q_EMIT changed();
    });
    connect(&installer_, &GuestDriverInstaller::succeeded, this, [this](bool restart) {
        install_progress_.clear();
        install_result_ = restart ? InstallResult::DoneRestart : InstallResult::Done;
        status_.refresh(Area::Graphics);
        Q_EMIT changed();
    });
    connect(&installer_, &GuestDriverInstaller::failed, this, &GraphicsAlerts::on_install_failed);
    connect(&update_, &GuestDriverUpdate::progress, this, [this](const QString& message) {
        install_progress_ = message;
        Q_EMIT changed();
    });
    connect(&update_, &GuestDriverUpdate::failed, this, &GraphicsAlerts::on_install_failed);
}

void GraphicsAlerts::on_install_failed(const QString& reason) {
    install_progress_.clear();
    install_result_ = InstallResult::Failed;
    install_error_ = reason;
    Q_EMIT changed();
}

void GraphicsAlerts::install_driver() {
    install_result_ = InstallResult::None;
    install_error_.clear();
    updating_ = false;
    (void)installer_.start();
    Q_EMIT changed();
}

void GraphicsAlerts::update_driver() {
    install_result_ = InstallResult::None;
    install_error_.clear();
    updating_ = true;
    (void)update_.start(platform::AccelVirtPaths::driver_tree());
    Q_EMIT changed();
}

QVector<GraphicsAlerts::Warning> GraphicsAlerts::warnings() const {
    const auto& g = status_.graphics();
    const bool running = status_.vm().running();
    QVector<Warning> out;
    if (g.probed && platform::HostGraphicsProbe::missing_required(g.apis)) {
        out.push_back({Warning::Kind::NoDirect3d, true,
                       QStringLiteral("The host serves no Direct3D."),
                       QStringLiteral("DXVK and vkd3d-proton were not found, and Windows draws "
                                      "its desktop with Direct3D 11, so it will keep restarting "
                                      "until they are.")});
    }
    if (g.guest.valid && running) {
        const services::GraphicsAdapter* display =
            g.adapters.isEmpty() ? nullptr : &g.adapters.first();
        if (display == nullptr || !display->guest) {
            out.push_back({Warning::Kind::DisplayNotRunning, true,
                           QStringLiteral("Windows is not running avgpu.sys."),
                           QStringLiteral("Windows lists no accel-virt adapter at all.")});
        } else if (display->guest->no_driver()) {
            out.push_back({Warning::Kind::DisplayNoDriver, true,
                           QStringLiteral("Windows has no driver for %1.")
                               .arg(platform::HostGpus::adapter_name(display->gpu)),
                           QStringLiteral("The machine's GPU was switched after the driver package "
                                          "was installed, and the adapter carries the new GPU's "
                                          "PCI ID, so Windows has not matched it to avgpu.sys "
                                          "yet.")});
        } else if (!display->guest->running()) {
            const quint32 code = display->guest->problem_code;
            out.push_back(
                {Warning::Kind::DisplayNotRunning, true,
                 QStringLiteral("Windows is not running avgpu.sys."),
                 code != 0 ? QStringLiteral("The adapter reports problem code %1: %2.")
                                 .arg(code)
                                 .arg(services::GuestGpuStatus::explain(code, g.guest.test_signing))
                           : QStringLiteral("The adapter has not started.")});
        }
    }
    if (installer_.running() || update_.uploading()) {
        out.push_back({Warning::Kind::Installing, false,
                       updating_ ? QStringLiteral("Updating the driver.")
                                 : QStringLiteral("Installing the driver for new adapters."),
                       install_progress_});
    } else if (install_result_ == InstallResult::Failed) {
        out.push_back({updating_ ? Warning::Kind::UpdateFailed : Warning::Kind::InstallFailed, true,
                       updating_ ? QStringLiteral("The driver update did not install.")
                                 : QStringLiteral("The driver for new adapters did not install."),
                       install_error_});
    } else if (install_result_ == InstallResult::DoneRestart) {
        out.push_back({Warning::Kind::InstallFinished, false,
                       QStringLiteral("The driver is installed."),
                       updating_
                           ? QStringLiteral("Windows starts the new driver after a restart.")
                           : QStringLiteral("Windows starts it on the new adapters after a restart.")});
    } else if (g.guest.valid && running) {
        const services::DriverManifest guest{g.guest.driver_manifest};
        const QStringList changed = guest.empty() || shipped_.empty()
                                        ? QStringList{}
                                        : shipped_.differences(guest);
        if (!changed.isEmpty()) {
            out.push_back({Warning::Kind::DriverUpdate, false,
                           QStringLiteral("A driver update is ready for the guest."),
                           QStringLiteral("LSW was updated with a different avgpu.sys package "
                                          "than Windows has: %1 %2 changed.")
                               .arg(changed.size())
                               .arg(changed.size() == 1 ? QStringLiteral("file")
                                                        : QStringLiteral("files"))});
        }
        for (const auto& adapter : g.adapters) {
            if (!adapter.display && (!adapter.guest || adapter.guest->no_driver())) {
                out.push_back({Warning::Kind::NoDriver, false,
                               QStringLiteral("Windows has no driver for %1.")
                                   .arg(platform::HostGpus::adapter_name(adapter.gpu)),
                               QStringLiteral("It was added as an adapter after the driver "
                                              "package was installed, so Windows has not matched "
                                              "it to avgpu.sys yet.")});
                break;
            }
        }
    }
    return out;
}

GraphicsAlerts::Severity GraphicsAlerts::worst() const {
    Severity out = Severity::None;
    for (const auto& warning : warnings()) {
        if (warning.critical) {
            return Severity::Red;
        }
        if (warning.kind != Warning::Kind::Installing) {
            out = Severity::Amber;
        }
    }
    return out;
}

}  // namespace lsw::host::manager
