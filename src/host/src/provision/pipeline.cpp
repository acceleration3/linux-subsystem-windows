#include "lsw/host/provision/pipeline.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/vm_name.hpp"
#include "lsw/host/provision/install/post_install.hpp"
#include "lsw/host/provision/iso/downloader.hpp"
#include "lsw/host/provision/ivshmem_driver_fetcher.hpp"
#include "lsw/host/provision/debloat_stager.hpp"
#include "lsw/host/provision/payload_iso_stager.hpp"
#include "lsw/host/provision/press_any_key_prompter.hpp"
#include "lsw/host/provision/setup_stage_runner.hpp"
#include "lsw/host/provision/virtio_win_fetcher.hpp"
#include "lsw/host/provision/vnc_port_resolver.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"

#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>

#include <limits>

namespace lsw::host::provision {

namespace {

// The agent's RPC port inside the guest. Fixed by the agent build.
constexpr quint32 kAgentPort = 5102;

// Row indices in the payload phase's UI table. Mirrored by
// PayloadPage's row labels.
constexpr int kPayloadRowVirtio = 0;
constexpr int kPayloadRowIvshmem = 1;
constexpr int kPayloadRowBuild = 2;

}  // namespace

Pipeline::Pipeline(services::HostServices& services, QObject* parent)
    : QObject(parent), services_(services) {}

Pipeline::~Pipeline() {
    // The setup runner holds channel event handlers. Detaching is
    // normally done by the terminal-state hooks; doing it again here is
    // a no-op and covers destruction along an unexpected path.
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
    // Defer one event-loop turn so the first wizard page has been shown
    // and its connections are live before any signal is emitted —
    // otherwise it misses the opening ones and renders blank.
    QTimer::singleShot(0, this, [this] {
        if (inputs_.iso_local_path.isEmpty()) {
            run_download();
        } else {
            run_trim_image();  // Windows ISO already local
        }
    });
}

void Pipeline::cancel() {
    if (cancelled_) {
        return;
    }
    cancelled_ = true;
    ++generation_;
    if (prompter_ != nullptr) prompter_->stop();
    if (vnc_resolver_ != nullptr) vnc_resolver_->stop();
    if (agent_watcher_ != nullptr) agent_watcher_->stop();
    if (downloader_ != nullptr) downloader_->cancel();
    if (setup_runner_ != nullptr) setup_runner_->detach();
}

void Pipeline::emit_status(Step step, const QString& detail) {
    Q_EMIT step_progress(step, std::numeric_limits<double>::quiet_NaN(), detail);
}

void Pipeline::emit_failed(Step step, const QString& error) {
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

// ----- Download the Windows ISO -------------------------------------------

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

// ----- Fetch virtio drivers, then build the payload ISO --------------------

// ----- Trim the Windows image, on the HOST ---------------------------------
//
// Before the payload ISO is built, because the trimmed install.wim is
// one of the things the payload ISO carries. The Windows ISO is read
// and never written: the answer file installs from the copy on the
// payload, so the source stays byte-identical for the next VM.
void Pipeline::run_trim_image() {
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
        // Everything downstream boots what iso_local_path names, so the
        // swap is the whole of the wiring. The source ISO is left alone
        // and stays reusable for the next VM.
        inputs_.iso_local_path = iso_path;
        Q_EMIT step_finished(Step::TrimmingImage);
        run_build_payload();
    });
    debloat_stager_->trim(inputs_.iso_local_path, services_.settings().isos_dir(),
                          inputs_.image_index, inputs_.debloat);
}

void Pipeline::run_build_payload() {
    Q_EMIT step_started(Step::BuildingPayloadIso,
                        QStringLiteral("Preparing the install payload..."));
    Q_EMIT payload_step_started(kPayloadRowVirtio);

    virtio_fetcher_ = new VirtioWinFetcher(this);
    connect(virtio_fetcher_, &VirtioWinFetcher::progress, this,
            [this](double fraction, const QString& detail) {
        Q_EMIT payload_step_progress(kPayloadRowVirtio, detail);
        Q_EMIT step_progress(Step::BuildingPayloadIso, fraction, detail);
    });
    connect(virtio_fetcher_, &VirtioWinFetcher::failed, this,
            [this](const QString& message) {
        Q_EMIT payload_step_finished(kPayloadRowVirtio, false, message);
        emit_failed(Step::BuildingPayloadIso,
                    QStringLiteral("virtio-win.iso: %1").arg(message));
    });
    connect(virtio_fetcher_, &VirtioWinFetcher::finished, this,
            [this](const QString& iso_path) {
        inputs_.virtio_iso_path = iso_path;
        Q_EMIT payload_step_finished(kPayloadRowVirtio, true, QStringLiteral("done"));
        run_fetch_ivshmem_driver();
    });
    virtio_fetcher_->fetch(inputs_.virtio_iso_path, services_.settings().isos_dir());
}

// ----- Fetch the ivshmem driver, on the HOST -------------------------------
//
// Before the payload ISO is built, because the ISO is what carries the
// driver into the offline image — and on the host, because the device
// the driver enables is the transport. A guest-side fetch would need
// the channel it is meant to provide.
//
// A failure here does not stop the install. The guest boots without the
// shared-memory device: no window mirroring, no shmem command stream,
// but a working Windows. That is worth having on a machine with no
// extractor installed, and it is reported in its own row rather than
// discovered later as a missing PCI device.

void Pipeline::run_fetch_ivshmem_driver() {
    Q_EMIT payload_step_started(kPayloadRowIvshmem);

    ivshmem_fetcher_ = new IvshmemDriverFetcher(this);
    const auto generation = generation_;
    connect(ivshmem_fetcher_, &IvshmemDriverFetcher::progress, this,
            [this, generation](double fraction, const QString& detail) {
        if (generation != generation_) return;
        Q_EMIT payload_step_progress(kPayloadRowIvshmem, detail);
        Q_EMIT step_progress(Step::BuildingPayloadIso, fraction, detail);
    });
    connect(ivshmem_fetcher_, &IvshmemDriverFetcher::failed, this,
            [this, generation](const QString& message) {
        if (generation != generation_) return;
        LSW_WARN("provision: ivshmem driver unavailable: {}", message.toStdString());
        Q_EMIT payload_step_finished(kPayloadRowIvshmem, false, message);
        run_build_payload_iso();
    });
    connect(ivshmem_fetcher_, &IvshmemDriverFetcher::finished, this,
            [this, generation](const QString& driver_dir) {
        if (generation != generation_) return;
        Q_EMIT payload_step_finished(kPayloadRowIvshmem, true, QStringLiteral("done"));
        static_cast<void>(driver_dir);  // PayloadIsoStager re-locates it
        run_build_payload_iso();
    });
    ivshmem_fetcher_->fetch(services_.settings().data_dir());
}

// ----- Build the payload ISO -----------------------------------------------

void Pipeline::run_build_payload_iso() {
    // Identifiers and paths all derive from the single-VM constants.
    record_ = config::VmRecord{};
    record_.uuid = QUuid::createUuid();
    record_.name = config::kVmName;
    record_.iso_path = inputs_.iso_local_path;
    record_.sizing = inputs_.sizing;
    const QString name = QLatin1String(config::kVmName);
    record_.disk_path = QDir(services_.settings().disks_dir())
                            .filePath(QStringLiteral("%1.qcow2").arg(name));
    record_.nvram_path = QDir(services_.settings().nvram_dir())
                             .filePath(QStringLiteral("%1.fd").arg(name));

    Q_EMIT payload_step_started(kPayloadRowBuild);
    emit_status(Step::BuildingPayloadIso,
                QStringLiteral("Building the LSW payload ISO..."));

    payload_stager_ = new PayloadIsoStager(services_.settings(), this);
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

// ----- Allocate the qcow2 system disk --------------------------------------

void Pipeline::run_create_disk() {
    Q_EMIT step_started(Step::CreatingDisk,
        QStringLiteral("Allocating the qcow2 system disk (%1 GiB)...")
            .arg(record_.sizing.disk_gib));

    auto* lifecycle = &services_.lifecycle();
    const QString disk_path = record_.disk_path;
    const int size_gib = record_.sizing.disk_gib;
    const auto generation = generation_;
    const QPointer<Pipeline> self{this};
    (void)QtConcurrent::run([self, lifecycle, disk_path, size_gib, generation]() {
        const int rc = lifecycle->create_disk(disk_path, size_gib);
        QMetaObject::invokeMethod(self.data(), [self, rc, generation]() {
            if (!self || self->generation_ != generation) return;
            if (rc != 0) {
                self->emit_failed(Step::CreatingDisk,
                                  QStringLiteral("qemu-img create failed"));
                return;
            }
            Q_EMIT self->step_finished(Step::CreatingDisk);
            self->run_start_vm();
        }, Qt::QueuedConnection);
    });
}

// ----- Define + start the domain -------------------------------------------

void Pipeline::run_start_vm() {
    Q_EMIT step_started(Step::StartingVm,
                        QStringLiteral("Defining the libvirt domain and starting the VM..."));

    // Persist the record: the manager UI and the post-install watcher
    // both look it up here.
    record_ = *services_.vms().find(services_.vms().add(record_));

    vm::DomainXmlInputs domain_inputs;
    domain_inputs.virtio_iso = inputs_.virtio_iso_path;
    if (services_.lifecycle().define_and_start(record_, domain_inputs) != 0) {
        emit_failed(Step::StartingVm,
                    QStringLiteral("libvirt rejected the domain — see manager logs"));
        return;
    }
    Q_EMIT step_finished(Step::StartingVm);

    // Both run alongside the agent-reachability wait.
    prompter_ = new PressAnyKeyPrompter(services_.libvirt(), this);
    prompter_->start(record_.uuid);

    vnc_resolver_ = new VncPortResolver(services_.lifecycle(), this);
    connect(vnc_resolver_, &VncPortResolver::resolved, this,
            [this](int port) { Q_EMIT vnc_port_available(port); });
    vnc_resolver_->start(record_.uuid);

    run_wait_for_agent();
}

// ----- Wait for the guest agent --------------------------------------------

void Pipeline::run_wait_for_agent() {
    Q_EMIT step_started(Step::WaitingForAgent,
        QStringLiteral("Waiting for the guest agent to come online "
                       "(Windows install typically takes 10–30 minutes)..."));

    agent_watcher_ =
        new install::PostInstallWatcher(record_.uuid, record_.vsock_cid, kAgentPort, this);
    connect(agent_watcher_, &install::PostInstallWatcher::ready, this, [this](const QUuid&) {
        Q_EMIT step_finished(Step::WaitingForAgent);
        // step_finished() makes InstallPage schedule wizard()->next() on
        // a zero-delay timer. That must run first, so SetupPage exists
        // and is connected before the plan signal fires — otherwise the
        // table never populates. Deferring queues us strictly after it.
        QTimer::singleShot(0, this, [this] { run_setup_phase(); });
    });
    agent_watcher_->start();
}

// ----- Run the post-install setup plan -------------------------------------

void Pipeline::run_setup_phase() {
    Q_EMIT step_started(Step::RunningSetup,
                        QStringLiteral("Running post-install setup steps..."));

    // Bind the shared channel — the same one the manager UI uses, so we
    // do not race a second connection to the agent.
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
        finish();
    });
    setup_runner_->run();
}

// ----- Done -----------------------------------------------------------------

void Pipeline::finish() {
    // Nothing to swap: the VM installed and booted on virtio from the
    // start, via the PnP-injected viostor driver. All that is left is to
    // eject the install media so reboots stop re-prompting the El-Torito
    // CD and the ISOs are freed. Ejecting CD-ROM *media* leaves the vda
    // system disk untouched.
    LSW_INFO("provision: setup complete; ejecting install media and finishing");
    (void)services_.lifecycle().eject_install_media(record_.uuid);
    emit_completed(record_.uuid);
}

}  // namespace lsw::host::provision
