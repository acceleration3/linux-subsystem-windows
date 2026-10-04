#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/pipeline_inputs.hpp"
#include "lsw/host/provision/pipeline_step.hpp"
#include "lsw/host/provision/setup_stage.hpp"

#include <QElapsedTimer>
#include <QObject>

#include <optional>
#include <QString>
#include <QUuid>
#include <QVector>

namespace lsw::host::services { class HostServices; }
namespace lsw::host::vm { struct DomainXmlInputs; }

class QTimer;

namespace lsw::host::provision {

namespace iso { class Downloader; }
namespace install { class PostInstallWatcher; }
class DebloatStager;
class PayloadIsoStager;
class PressAnyKeyPrompter;
class SetupStageRunner;
class VncPortResolver;

// Sequences a first-run install, headlessly.
//
// One concern: what happens next. Each phase is owned by a collaborator
// — trim the image, build the payload, run the setup plan — and the
// Pipeline decides the order, forwards their progress as ``step_*``
// signals, and stops the whole thing on the first failure.
//
// Pure QObject, no widget dependencies, because it has two consumers
// that render it completely differently: the Qt wizard, whose pages
// subscribe to the subset each cares about, and the ``--unattended``
// CLI, which prints the same signals as lines of text.
//
// Threading: lives on the GUI thread. Blocking work (qemu-img, libvirt
// shutdown polling, agent RPCs) goes to QtConcurrent workers and comes
// back through queued connections, so pipeline state is only ever
// mutated on the GUI thread.
class Pipeline : public QObject {
    Q_OBJECT
public:
    // Re-exported so existing call sites keep reading ``Pipeline::Step``.
    using Step = PipelineStep;

    explicit Pipeline(services::HostServices& services, QObject* parent = nullptr);
    ~Pipeline() override;

    void set_inputs(PipelineInputs in);
    [[nodiscard]] const PipelineInputs& inputs() const noexcept { return inputs_; }

    // Begins the run. Safe to call once; later calls are no-ops.
    void start();

    // Best-effort teardown. Stops timers and watchers; in-flight
    // blocking workers are not cancelled (they would need cooperative
    // checks we do not have), but their results are dropped via a
    // generation token.
    void cancel();

    [[nodiscard]] const config::VmRecord& vm_record() const noexcept { return record_; }

    // The parsed setup plan, empty until the setup phase has parsed it.
    // SetupPage reads this in initializePage() as a belt-and-braces
    // guard against connecting after ``setup_plan_ready`` already fired.
    [[nodiscard]] const QVector<SetupStage>& setup_plan() const noexcept { return plan_; }

    // The step last started, its description, and the VNC port once one
    // is known: a page that connects after the signal fired (the install
    // page is shown a turn after the disk step has begun) reads these.
    [[nodiscard]] std::optional<Step> current_step() const noexcept { return current_step_; }
    [[nodiscard]] const QString& current_description() const noexcept {
        return current_description_;
    }
    [[nodiscard]] int vnc_port() const noexcept { return vnc_port_; }

Q_SIGNALS:
    // ``description`` is a status line suitable for a single label.
    void step_started(Step step, QString description);
    void step_progress(Step step, double fraction, QString detail);
    void step_finished(Step step);
    void failed(Step step, QString error);
    void completed(QUuid vm_uuid);

    // The install media's VNC port, once libvirt has assigned one. The
    // wizard attaches a VncView; the CLI logs it for virt-viewer.
    void vnc_port_available(int port);

    // Setup-phase plan, before any step dispatches, so the UI can
    // populate its table with every row in a pending state.
    void setup_plan_ready(QVector<SetupStage> stages);

    // Per-setup-step. ``row`` is a global index across all stages.
    void setup_step_started(int row);
    void setup_step_progress(int row, QString detail);
    void setup_step_finished(int row, bool ok, QString detail);

    // Per-task signals for the payload phase, so PayloadPage can render
    // a table like the setup one. One fixed row: 0 = build the LSW
    // payload ISO.
    void payload_step_started(int row);
    void payload_step_progress(int row, QString detail);
    void payload_step_finished(int row, bool ok, QString detail);

private:
    // Phase entry points, in order. Each emits ``step_started`` and
    // arranges for the next once its work completes.
    void run_download();
    void run_trim_image();
    void run_build_payload();
    void run_create_disk();
    void run_start_vm();
    void run_wait_for_agent();
    void run_setup_phase();
    // After setup: the domain is redefined without the install-time VGA
    // adapter and restarted, which is also the first boot that loads the
    // display driver SetupComplete installed. Shut down, start, and wait
    // for the agent again.
    void run_restart_vm();
    void poll_shutdown();
    void run_start_steady_vm();
    void run_wait_for_agent_after_restart();
    void finish();

    // The domain inputs for this VM, from settings like every other
    // definition the manager writes. ``install_display`` adds the VGA adapter Windows Setup
    // needs, since the accel-virt device shows nothing until its driver
    // is installed.
    [[nodiscard]] vm::DomainXmlInputs domain_inputs(bool install_display) const;

    // Terminal emit helpers. Use these rather than raw Q_EMIT so the
    // setup runner is always detached from the channel before consumers
    // see the signal and start destroying things in response.
    void emit_failed(Step step, const QString& error);
    void emit_completed(const QUuid& uuid);

    // Status line with no meaningful fraction.
    void emit_status(Step step, const QString& detail);

    services::HostServices& services_;
    PipelineInputs inputs_;
    config::VmRecord record_;

    bool started_ = false;
    bool cancelled_ = false;
    // Bumped by cancel(); workers compare against it to drop stale replies.
    quint64 generation_ = 0;

    // Phase collaborators. All QObject children of this.
    iso::Downloader* downloader_ = nullptr;
    DebloatStager* debloat_stager_ = nullptr;
    PayloadIsoStager* payload_stager_ = nullptr;
    install::PostInstallWatcher* agent_watcher_ = nullptr;
    PressAnyKeyPrompter* prompter_ = nullptr;
    VncPortResolver* vnc_resolver_ = nullptr;
    SetupStageRunner* setup_runner_ = nullptr;

    // The restart into the steady-state machine.
    QTimer* shutdown_poll_ = nullptr;
    QElapsedTimer shutdown_clock_;
    int shutdown_ticks_ = 0;
    QTimer* restart_deadline_ = nullptr;

    QVector<SetupStage> plan_;
    std::optional<Step> current_step_;
    QString current_description_;
    int vnc_port_ = -1;
};

}  // namespace lsw::host::provision
