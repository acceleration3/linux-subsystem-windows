#pragma once

#include "lsw/host/provision/setup_stage.hpp"
#include "lsw/host/provision/setup_step.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::provision {

// Runs the post-install setup plan inside the guest.
//
// Stages run in sequence, every step within a stage in parallel. That
// shape is the point: steps that must observe each other's results (a
// runtime installed before the thing that links against it) go in later
// stages, everything else overlaps.
//
// Dispatch is a blocking RPC that returns a job id almost immediately;
// the work itself happens in the guest and streams back as
// ``setup_progress`` / ``setup_done`` events. The job-id-to-row map is
// what turns those asynchronous events back into UI table rows.
class SetupStageRunner : public QObject {
    Q_OBJECT
public:
    explicit SetupStageRunner(link::AgentRpcClient& rpc, QObject* parent = nullptr);
    ~SetupStageRunner() override;

    // Parse the embedded plan and start stage 0. Emits ``plan_ready``
    // before any step is dispatched so the UI can lay out its rows.
    void run();

    // Unregister the event handlers. Called before the owner emits a
    // terminal signal, while the channel is still alive — the reader
    // thread must not deliver into a half-destroyed runner. Idempotent.
    void detach();

Q_SIGNALS:
    // Emitted once, before any dispatch.
    void plan_ready(const QVector<SetupStage>& stages);

    // ``row`` is a global index across all stages, laid out in stage
    // order with no gaps — which is what the UI table expects.
    void step_started(int row);
    void step_progress(int row, const QString& detail);
    void step_finished(int row, bool ok, const QString& detail);

    // Every stage has completed. ``all_ok`` is false if any step failed.
    void finished(bool all_ok);

private:
    void subscribe_to_agent_events();
    void start_stage(int index);
    void on_stage_step_finished();
    void finish();

    // Fire one step at the agent. Returns false when the RPC itself
    // failed — in that case no ``setup_done`` will ever arrive for it,
    // so the caller must not count it as pending.
    [[nodiscard]] bool dispatch_step(const SetupStep& step, int row);

    link::AgentRpcClient* rpc_;  // cleared by detach()
    unsigned long long progress_token_ = 0;
    unsigned long long done_token_ = 0;

    QVector<SetupStage> stages_;
    int current_stage_ = -1;
    int stage_pending_ = 0;
    QHash<QString, int> job_to_row_;  // agent job id -> UI row
    bool any_failure_ = false;
};

}  // namespace lsw::host::provision
