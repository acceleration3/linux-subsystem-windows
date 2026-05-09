#pragma once

#include "lsw/manager/config/vm_state.hpp"
#include "lsw/manager/iso/downloader.hpp"
#include "lsw/manager/wizard/setup_plan.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUuid>
#include <QVector>

namespace lsw::manager {
class App;
}
namespace lsw::manager::install {
class PostInstallWatcher;
}
namespace lsw::manager::vm {
class AgentRpcClient;
}

namespace lsw::manager::wizard {

// Headless install pipeline. Owns every long-running step the wizard
// would otherwise run inline on a QWizardPage: ISO download, payload
// ISO build, qcow2 disk creation, libvirt define + start, press-any-key
// spam, agent reachability probe, setup-step dispatch, ACPI shutdown,
// virtio-swap, post-swap agent probe.
//
// Pure QObject — no widget dependencies. Two consumers exist:
//
//   * The Qt wizard UI. Each long-running page subscribes to the
//     subset of signals it cares about, drives its widgets, and calls
//     ``wizard()->next()`` when its phase ends.
//   * The unattended CLI runner. Constructs a Pipeline directly on the
//     ``App``, prints step transitions / progress to stdout, exits on
//     ``completed`` / ``failed``.
//
// Threading: lives on the GUI thread. Blocking work (qemu-img, libvirt
// shutdown polling, AgentRpcClient::call) is dispatched to QtConcurrent
// workers; replies are marshalled back via QueuedConnection. The
// pipeline only mutates its own state on the GUI thread.
class Pipeline : public QObject {
    Q_OBJECT
public:
    // Coarse-grained step identifiers. The setup phase reports per-step
    // progress via dedicated ``setup_*`` signals because each setup
    // step has a name + kind + row index the UI wants to display
    // independently of the coarse Step.
    enum class Step {
        DownloadingIso,           // skipped if iso_local_path supplied
        BuildingPayloadIso,       // also fetches latest virtio-win.iso
        CreatingDisk,
        StartingVm,               // define + start pre-virtio domain
        WaitingForAgent,          // press-any-key spam runs alongside
        RunningSetup,             // see setup_* signals for per-step detail
        ShuttingDown,
        StartingVmVirtio,         // redefine + start post-virtio domain
        WaitingForAgentAfterSwap,
    };
    Q_ENUM(Step)

    // Inputs collected by the wizard pages (or by the CLI). The ISO
    // source is exclusive: ``iso_local_path`` takes precedence; if
    // empty, ``iso_download_url`` is used. Producing ``iso_download_url``
    // from a Microsoft edition+language pair is the caller's job — see
    // ``MicrosoftResolver``.
    struct Inputs {
        QString iso_local_path;
        QString iso_download_url;
        QString iso_display_label;
        QString virtio_iso_path;
        QString user_name;
        QString user_password;
        QString computer_name;  // Windows hostname; empty → VM name
        config::VmSizing sizing;
        // 1-based WIM image index (which Windows edition to install).
        // Resolved from the ISO via install::list_iso_editions().
        int image_index = 1;
    };

    explicit Pipeline(App& app, QObject* parent = nullptr);
    ~Pipeline() override;

    void set_inputs(Inputs in);
    [[nodiscard]] const Inputs& inputs() const noexcept { return inputs_; }

    // Begins the pipeline. Safe to call once. Subsequent calls are no-ops.
    void start();

    // Best-effort teardown. Stops timers and watchers; in-flight
    // blocking workers are NOT cancelled (they'd require cooperative
    // checks we don't have), but their results are dropped via a
    // generation token.
    void cancel();

    [[nodiscard]] const config::VmRecord& vm_record() const noexcept { return rec_; }

    // The parsed setup plan, or empty until the setup phase has parsed
    // it. SetupPage pulls this in initializePage() as a belt-and-
    // suspenders against connecting after ``setup_plan_ready`` fired.
    [[nodiscard]] const QVector<SetupStage>& setup_plan() const noexcept { return stages_; }

Q_SIGNALS:
    // Coarse-grained phase signals. ``description`` is a human-readable
    // status line suitable for a single-line label.
    void step_started(Step step, QString description);
    void step_progress(Step step, double fraction, QString detail);
    void step_finished(Step step);
    void failed(Step step, QString error);
    void completed(QUuid vm_uuid);

    // Emitted once when the install ISO's VNC graphics port is known.
    // The wizard UI's InstallPage attaches its VncView to it; the CLI
    // logs the port for "attach virt-viewer manually" workflows.
    void vnc_port_available(int port);

    // Setup-phase plan ready. Emitted once after parsing setup-steps.json,
    // before any step dispatches. The UI consumes this to populate the
    // setup table with all rows in their initial Pending state.
    void setup_plan_ready(QVector<SetupStage> stages);

    // Per-setup-step signals. ``row`` is the global row index across
    // all stages (rows are laid out in stage order, no gaps), matching
    // what the UI table expects.
    void setup_step_started(int row);
    void setup_step_progress(int row, QString detail);
    void setup_step_finished(int row, bool ok, QString detail);

    // Per-task signals for the BuildingPayloadIso stage, so PayloadPage
    // can render a SetupPage-style table. Two fixed rows:
    //   0 = download virtio-win.iso, 1 = build LSW payload ISO.
    void payload_step_started(int row);
    void payload_step_progress(int row, QString detail);
    void payload_step_finished(int row, bool ok, QString detail);

private:
    // Phase entry points. Each one emits ``step_started`` and arranges
    // for a follow-up call once its work completes.
    void run_download();
    void run_build_payload();
    // Second half of run_build_payload, after virtio-win.iso is on
    // disk: actually stage + build the LSW payload ISO.
    void build_payload_iso();
    void run_create_disk();
    void run_start_vm();
    void run_wait_for_agent();
    void run_setup_phase();
    void run_post_setup_swap();

    // Terminal-state teardown — unregisters the rpc event handlers so
    // the reader thread can't deliver another callback into a Pipeline
    // that's about to be destroyed. Called *before* emitting
    // ``failed`` / ``completed``, while ``rpc_`` is still alive (it's
    // a value member of ``App`` and may be destroyed before us during
    // App teardown). Idempotent.
    void detach_rpc_handlers();

    // Terminal-state emit helpers. Use these instead of raw
    // ``Q_EMIT failed`` / ``Q_EMIT completed`` so the rpc handler
    // detach happens consistently before consumers see the signal
    // (and start destroying things in response).
    void emit_failed(Step step, const QString& error);
    void emit_completed(const QUuid& uuid);

    // Setup-phase helpers (parallel within stage, sequential between).
    void start_stage(int idx);
    [[nodiscard]] bool dispatch_step(const SetupStep& s, int row);
    void on_stage_step_finished();

    // Drop-in for ``LSW_INFO``/``LSW_WARN`` plus a ``step_progress``
    // emit of fraction=NaN with a status detail. Reserved for verbose
    // status lines that don't carry a fraction.
    void emit_status(Step step, const QString& detail);

    // Helper run on a worker thread. Posts results back via
    // QueuedConnection so all signal emission stays on the GUI thread.
    void perform_virtio_swap_worker();

    // Press-any-key timer tick.
    void on_press_any_key_tick();
    // VNC port-resolve retry tick.
    void on_vnc_retry_tick();

    App& app_;
    Inputs inputs_;
    config::VmRecord rec_;

    bool started_ = false;
    bool cancelled_ = false;
    quint64 generation_ = 0;  // bumped by cancel(); workers compare to drop stale replies

    // Phase state.
    iso::Downloader* downloader_ = nullptr;  // child object, owned by ``this``
    install::PostInstallWatcher* watcher_ = nullptr;
    QTimer* press_any_key_timer_ = nullptr;
    int press_any_key_ticks_remaining_ = 0;
    QTimer* vnc_retry_timer_ = nullptr;
    int vnc_retry_attempts_ = 0;
    int vnc_port_ = -1;

    // Setup-phase state.
    vm::AgentRpcClient* rpc_ = nullptr;  // non-owning — App owns the channel
    quint64 setup_progress_token_ = 0;
    quint64 setup_done_token_ = 0;
    QVector<SetupStage> stages_;
    int current_stage_ = -1;
    int stage_pending_ = 0;
    QHash<QString, int> job_to_row_;  // agent job_id → setup row index
    bool any_setup_failure_ = false;
    bool swap_started_ = false;
};

}  // namespace lsw::manager::wizard
