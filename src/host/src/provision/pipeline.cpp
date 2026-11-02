#include "lsw/host/provision/pipeline.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/vm_name.hpp"
#include "lsw/host/provision/debloat_stager.hpp"
#include "lsw/host/provision/install/no_prompt_media.hpp"
#include "lsw/host/provision/install/post_install.hpp"
#include "lsw/host/provision/iso/downloader.hpp"
#include "lsw/host/provision/payload_iso_stager.hpp"
#include "lsw/host/provision/setup_stage_runner.hpp"
#include "lsw/host/provision/vnc_port_resolver.hpp"
#include "lsw/host/services/domain_inputs.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"
#include "lsw/host/vm/graceful_shutdown.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QMetaEnum>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>

#include <limits>

namespace lsw::host::provision {

namespace {
constexpr quint32 kAgentPort = 5102;

constexpr int kPayloadRowBuild = 0;

constexpr int kShutdownPollMs = 2000;
constexpr qint64 kShutdownDeadlineMs = 5 * 60 * 1000;
constexpr int kShutdownRepressTicks = 15;
constexpr int kAgentAfterRestartDeadlineMs = 10 * 60 * 1000;

const char* step_name(PipelineStep step) {
    return QMetaEnum::fromType<PipelineStep>().valueToKey(static_cast<int>(step));
}

}

Pipeline::Pipeline(services::HostServices& services, QObject* parent)
    : QObject(parent), services_(services) {
    connect(this, &Pipeline::step_started, this, [this](Step step, const QString& description) {
        current_step_ = step;
        current_description_ = description;
        step_clock_.start();
        LSW_INFO("pipeline: step {} started", step_name(step));
    });
    connect(this, &Pipeline::step_finished, this, [this](Step step) {
        LSW_INFO("pipeline: step {} finished in {:.1f} s", step_name(step),
                 static_cast<double>(step_clock_.elapsed()) / 1000.0);
    });
    connect(this, &Pipeline::vnc_port_available, this, [this](int port) { vnc_port_ = port; });
}

Pipeline::~Pipeline() {
    if (setup_runner_ != nullptr) {
        setup_runner_->detach();
    }
}

void Pipeline::set_inputs(PipelineInputs in) { inputs_ = std::move(in); }

void Pipeline::start() {
    if (started_) {
        return;
    }
    started_ = true;
    QTimer::singleShot(0, this, [this] {
        if (inputs_.iso_local_path.isEmpty()) {
            run_download();
        } else {
            run_trim_image();
        }
    });
}

void Pipeline::cancel() {
    if (cancelled_) {
        return;
    }
    cancelled_ = true;
    ++generation_;
    if (vnc_resolver_ != nullptr) vnc_resolver_->stop();
    if (agent_watcher_ != nullptr) agent_watcher_->stop();
    if (shutdown_poll_ != nullptr) shutdown_poll_->stop();
    if (restart_deadline_ != nullptr) restart_deadline_->stop();
    if (downloader_ != nullptr) downloader_->cancel();
    if (setup_runner_ != nullptr) setup_runner_->detach();
}

void Pipeline::emit_status(Step step, const QString& detail) {
    Q_EMIT step_progress(step, std::numeric_limits<double>::quiet_NaN(), detail);
}

void Pipeline::emit_failed(Step step, const QString& error) {
    LSW_WARN("pipeline: step {} failed after {:.1f} s: {}", step_name(step),
             static_cast<double>(step_clock_.elapsed()) / 1000.0, error.toStdString());
    if (setup_runner_ != nullptr) {
        setup_runner_->detach();
    }
    Q_EMIT failed(step, error);
}

void Pipeline::emit_completed(const QUuid& uuid) {
    if (setup_runner_ != nullptr) {
        setup_runner_->detach();
    }
    Q_EMIT completed(uuid);
}

void Pipeline::run_download() {
    Q_EMIT step_started(Step::DownloadingIso,
        QStringLiteral("Downloading %1...")
            .arg(inputs_.iso_display_label.isEmpty() ? QStringLiteral("Windows")
                                                     : inputs_.iso_display_label));
    if (inputs_.iso_download_url.isEmpty()) {
        emit_failed(Step::DownloadingIso,
                    QStringLiteral("no download URL supplied and no local ISO provided"));
        return;
    }
    const QString dest = QDir(services_.settings().isos_dir()).filePath("windows.iso");

    downloader_ = new iso::Downloader(this);
    connect(downloader_, &iso::Downloader::progress, this,
            [this](qint64 received, qint64 total) {
        const double fraction =
            total > 0 ? static_cast<double>(received) / static_cast<double>(total) : -1.0;
        Q_EMIT step_progress(Step::DownloadingIso, fraction,
            QStringLiteral("Downloaded %1 MiB%2")
                .arg(received / (1024 * 1024))
                .arg(total > 0 ? QStringLiteral(" / %1 MiB").arg(total / (1024 * 1024))
                               : QString{}));
    });
    connect(downloader_, &iso::Downloader::finished, this, [this](const QString& path) {
        inputs_.iso_local_path = path;
        Q_EMIT step_finished(Step::DownloadingIso);
        run_trim_image();
    });
    connect(downloader_, &iso::Downloader::failed, this, [this](const QString& message) {
        emit_failed(Step::DownloadingIso, message);
    });
    downloader_->start(inputs_.iso_download_url, dest);
}

void Pipeline::run_trim_image() {
    if (inputs_.image_already_trimmed) {
        LSW_INFO("pipeline: image already trimmed; skipping");
        inputs_.image_index = 1;
        run_build_payload();
        return;
    }
    if (inputs_.debloat == debloat::DebloatLevel::None) {
        LSW_INFO("pipeline: no trim level chosen; installing the image as it is");
        run_build_payload();
        return;
    }
    Q_EMIT step_started(Step::TrimmingImage, QStringLiteral("Trimming the Windows image..."));

    debloat_stager_ = new DebloatStager(this);
    connect(debloat_stager_, &DebloatStager::progress, this,
            [this](const QString& detail, int percent) {
        Q_EMIT step_progress(Step::TrimmingImage, percent / 100.0, detail);
    });
    connect(debloat_stager_, &DebloatStager::failed, this, [this](const QString& message) {
        emit_failed(Step::TrimmingImage, message);
    });
    connect(debloat_stager_, &DebloatStager::finished, this, [this](const QString& iso_path) {
        inputs_.iso_local_path = iso_path;
        inputs_.image_index = 1;
        Q_EMIT step_finished(Step::TrimmingImage);
        run_build_payload();
    });
    debloat_stager_->trim(inputs_.iso_local_path, services_.settings().isos_dir(),
                          inputs_.image_index, inputs_.debloat);
}

void Pipeline::run_build_payload() {
    Q_EMIT step_started(Step::BuildingPayloadIso,
                        QStringLiteral("Preparing the install payload..."));

    record_ = config::VmRecord{};
    record_.uuid = QUuid::createUuid();
    record_.name = config::kVmName;
    record_.iso_path = inputs_.iso_local_path;
    record_.install_media_path = services_.settings().vm_install_media_path();
    record_.sizing = inputs_.sizing;
    record_.disk_path = services_.settings().vm_disk_path();
    record_.nvram_path = services_.settings().vm_nvram_path();

    Q_EMIT payload_step_started(kPayloadRowBuild);
    emit_status(Step::BuildingPayloadIso,
                QStringLiteral("Building the LSW payload ISO..."));

    {
        using K = config::ManagerSettings::Key;
        auto& settings = services_.manager_settings();
        settings.set_value(QLatin1String(K::kGuestUser), inputs_.user_name);
        settings.set_value(QLatin1String(K::kGuestPassword), inputs_.user_password);
    }
    payload_stager_ = new PayloadIsoStager(services_.settings(), services_.manager_settings(), this);
    const auto generation = generation_;
    connect(payload_stager_, &PayloadIsoStager::failed, this,
            [this, generation](const QString& message) {
        if (generation != generation_) return;
        Q_EMIT payload_step_finished(kPayloadRowBuild, false,
                                     QStringLiteral("build failed"));
        emit_failed(Step::BuildingPayloadIso, message);
    });
    connect(payload_stager_, &PayloadIsoStager::finished, this,
            [this, generation](const QString& payload_path) {
        if (generation != generation_) return;
        record_.payload_iso_path = payload_path;
        Q_EMIT payload_step_finished(kPayloadRowBuild, true, QStringLiteral("done"));
        Q_EMIT step_finished(Step::BuildingPayloadIso);
        run_create_disk();
    });
    payload_stager_->build(record_, inputs_);
}

void Pipeline::run_create_disk() {
    Q_EMIT step_started(Step::CreatingDisk,
        QStringLiteral("Allocating the qcow2 system disk (%1 GiB)...")
            .arg(record_.sizing.disk_gib));

    const config::VmRecord* old = services_.vms().primary();
    if (old == nullptr || !services_.lifecycle().is_running(old->uuid)) {
        replace_machine();
        return;
    }
    Q_EMIT step_progress(Step::CreatingDisk, -1.0,
                         QStringLiteral("Shutting the current guest down..."));
    auto* stop = new vm::GracefulShutdown(services_.lifecycle(), old->uuid,
                                          vm::GracefulShutdown::kDefaultDeadlineMs, this);
    const auto generation = generation_;
    connect(stop, &vm::GracefulShutdown::finished, this, [this, stop, generation](bool forced) {
        stop->deleteLater();
        if (generation != generation_) return;
        if (forced) {
            LSW_WARN("provision: the current guest did not shut down in time and was stopped");
        }
        Q_EMIT step_progress(Step::CreatingDisk, -1.0,
                             QStringLiteral("Allocating the qcow2 system disk (%1 GiB)...")
                                 .arg(record_.sizing.disk_gib));
        replace_machine();
    });
    stop->start();
}

void Pipeline::replace_machine() {
    QString replace_error;
    if (!services_.lifecycle().remove_existing(record_.name, &replace_error)) {
        emit_failed(Step::CreatingDisk, replace_error);
        return;
    }
    services_.vms().clear();

    auto* lifecycle = &services_.lifecycle();
    const QString disk_path = record_.disk_path;
    const int size_gib = record_.sizing.disk_gib;
    const QString iso_path = record_.iso_path;
    const QString media_path = record_.install_media_path;
    const auto generation = generation_;
    const QPointer<Pipeline> self{this};
    (void)QtConcurrent::run([self, lifecycle, disk_path, size_gib, iso_path, media_path,
                             generation]() {
        QString error;
        if (lifecycle->create_disk(disk_path, size_gib) != 0) {
            error = QStringLiteral("qemu-img create failed");
        } else if (QString why; !install::NoPromptMedia::make(iso_path, media_path, &why)) {
            error = QStringLiteral("the install media could not be prepared: %1").arg(why);
        }
        QMetaObject::invokeMethod(self.data(), [self, error, generation]() {
            if (!self || self->generation_ != generation) return;
            if (!error.isEmpty()) {
                self->emit_failed(Step::CreatingDisk, error);
                return;
            }
            Q_EMIT self->step_finished(Step::CreatingDisk);
            self->run_start_vm();
        }, Qt::QueuedConnection);
    });
}

void Pipeline::run_start_vm() {
    Q_EMIT step_started(Step::StartingVm,
                        QStringLiteral("Defining the libvirt domain and starting the VM..."));

    record_ = *services_.vms().find(services_.vms().add(record_));

    if (services_.lifecycle().define_and_start(record_, domain_inputs( true))
        != 0) {
        const QString& why = services_.lifecycle().last_error();
        emit_failed(Step::StartingVm,
                    why.isEmpty() ? QStringLiteral("the virtual machine could not be started")
                                  : QStringLiteral("the virtual machine could not be started: %1")
                                        .arg(why));
        return;
    }
    Q_EMIT step_finished(Step::StartingVm);

    vnc_resolver_ = new VncPortResolver(services_.lifecycle(), this);
    connect(vnc_resolver_, &VncPortResolver::resolved, this,
            [this](int port) { Q_EMIT vnc_port_available(port); });
    vnc_resolver_->start(record_.uuid);

    run_wait_for_agent();
}

void Pipeline::run_wait_for_agent() {
    Q_EMIT step_started(Step::WaitingForAgent,
        QStringLiteral("Waiting for the guest agent to come online "
                       "(Windows install typically takes 10–30 minutes)..."));

    agent_watcher_ =
        new install::PostInstallWatcher(record_.uuid, record_.vsock_cid, kAgentPort, this);
    connect(agent_watcher_, &install::PostInstallWatcher::ready, this, [this](const QUuid&) {
        Q_EMIT step_finished(Step::WaitingForAgent);
        QTimer::singleShot(0, this, [this] { run_setup_phase(); });
    });
    agent_watcher_->start();
}

void Pipeline::run_setup_phase() {
    Q_EMIT step_started(Step::RunningSetup,
                        QStringLiteral("Running post-install setup steps..."));

    services_.rpc().set_target(record_.vsock_cid, kAgentPort);

    setup_runner_ = new SetupStageRunner(services_.rpc(), this);
    connect(setup_runner_, &SetupStageRunner::plan_ready, this,
            [this](const QVector<SetupStage>& stages) {
        plan_ = stages;
        Q_EMIT setup_plan_ready(stages);
    });
    connect(setup_runner_, &SetupStageRunner::step_started, this,
            [this](int row) { Q_EMIT setup_step_started(row); });
    connect(setup_runner_, &SetupStageRunner::step_progress, this,
            [this](int row, const QString& detail) {
        Q_EMIT setup_step_progress(row, detail);
    });
    connect(setup_runner_, &SetupStageRunner::step_finished, this,
            [this](int row, bool ok, const QString& detail) {
        Q_EMIT setup_step_finished(row, ok, detail);
    });
    connect(setup_runner_, &SetupStageRunner::finished, this, [this](bool all_ok) {
        Q_EMIT step_finished(Step::RunningSetup);
        if (!all_ok) {
            emit_failed(Step::RunningSetup,
                        QStringLiteral("one or more setup steps failed"));
            return;
        }
        run_restart_vm();
    });
    setup_runner_->run();
}

void Pipeline::run_restart_vm() {
    Q_EMIT step_started(Step::ShuttingDown,
                        QStringLiteral("Shutting Windows down to restart it on its own display "
                                       "driver..."));

    record_.first_boot_done = true;
    if (auto* stored = services_.vms().find(record_.uuid); stored != nullptr) {
        stored->first_boot_done = true;
    }
    if (services_.lifecycle().redefine(record_, domain_inputs( false)) != 0) {
        emit_failed(Step::ShuttingDown,
                    QStringLiteral("libvirt refused the VM's steady-state definition, so it still "
                                   "has the install-time VGA adapter (see manager logs)"));
        return;
    }
    if (!services_.lifecycle().is_running(record_.uuid)) {
        run_start_steady_vm();
        return;
    }
    if (services_.lifecycle().shutdown(record_.uuid) != 0) {
        LSW_WARN("provision: the first shutdown request was refused; retrying");
    }

    shutdown_clock_.start();
    shutdown_ticks_ = 0;
    shutdown_poll_ = new QTimer(this);
    shutdown_poll_->setInterval(kShutdownPollMs);
    connect(shutdown_poll_, &QTimer::timeout, this, &Pipeline::poll_shutdown);
    shutdown_poll_->start();
}

void Pipeline::poll_shutdown() {
    auto& lifecycle = services_.lifecycle();
    if (!lifecycle.is_running(record_.uuid)) {
        shutdown_poll_->stop();
        shutdown_poll_->deleteLater();
        shutdown_poll_ = nullptr;
        run_start_steady_vm();
        return;
    }
    if (shutdown_clock_.elapsed() >= kShutdownDeadlineMs) {
        LSW_WARN("provision: the guest did not shut down within {} s; forcing it off",
                 kShutdownDeadlineMs / 1000);
        (void)lifecycle.force_off(record_.uuid);
        return;
    }
    if (++shutdown_ticks_ % kShutdownRepressTicks == 0) {
        (void)lifecycle.shutdown(record_.uuid);
    }
}

void Pipeline::run_start_steady_vm() {
    Q_EMIT step_finished(Step::ShuttingDown);
    Q_EMIT step_started(Step::StartingVmVirtio,
                        QStringLiteral("Starting the VM on its accel-virt display..."));
    if (services_.lifecycle().start(record_.uuid, true) != 0) {
        emit_failed(Step::StartingVmVirtio,
                    QStringLiteral("libvirt would not start the VM on its steady-state "
                                   "definition (see manager logs)"));
        return;
    }
    Q_EMIT step_finished(Step::StartingVmVirtio);
    run_wait_for_agent_after_restart();
}

void Pipeline::run_wait_for_agent_after_restart() {
    Q_EMIT step_started(Step::WaitingForAgentAfterSwap,
                        QStringLiteral("Waiting for the guest agent on the first boot with the "
                                       "display driver..."));
    if (agent_watcher_ != nullptr) {
        agent_watcher_->stop();
        agent_watcher_->deleteLater();
    }
    agent_watcher_ =
        new install::PostInstallWatcher(record_.uuid, record_.vsock_cid, kAgentPort, this);
    restart_deadline_ = new QTimer(this);
    restart_deadline_->setSingleShot(true);
    restart_deadline_->setInterval(kAgentAfterRestartDeadlineMs);
    connect(agent_watcher_, &install::PostInstallWatcher::ready, this, [this](const QUuid&) {
        restart_deadline_->stop();
        Q_EMIT step_finished(Step::WaitingForAgentAfterSwap);
        finish();
    });
    connect(restart_deadline_, &QTimer::timeout, this, [this] {
        agent_watcher_->stop();
        emit_failed(Step::WaitingForAgentAfterSwap,
                    QStringLiteral("the guest agent did not answer within %1 minutes of the "
                                   "restart onto the accel-virt display. If the guest is "
                                   "rebooting in a loop, avgpu.sys is failing to start: "
                                   "C:\\ProgramData\\LSW\\logs\\promote.log in the guest has the "
                                   "driver install's output")
                        .arg(kAgentAfterRestartDeadlineMs / 60000));
    });
    restart_deadline_->start();
    agent_watcher_->start();
}

void Pipeline::finish() {
    LSW_INFO("provision: the guest is up on its steady-state definition; finishing");
    emit_completed(record_.uuid);
}

vm::DomainXmlInputs Pipeline::domain_inputs(bool install_display) const {
    vm::DomainXmlInputs inputs =
        services::domain_inputs_from_settings(services_.settings(), services_.manager_settings());
    inputs.install_display = install_display;
    return inputs;
}

}
