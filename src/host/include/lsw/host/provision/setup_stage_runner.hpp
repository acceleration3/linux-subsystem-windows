#pragma once

#include "lsw/host/link/event_subscription.hpp"
#include "lsw/host/provision/setup_stage.hpp"
#include "lsw/host/provision/setup_step.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::provision {
class SetupStageRunner : public QObject {
    Q_OBJECT
public:
    explicit SetupStageRunner(link::AgentRpcClient& rpc, QObject* parent = nullptr);
    ~SetupStageRunner() override;

    void run();

    void detach();

Q_SIGNALS:
    void plan_ready(const QVector<SetupStage>& stages);

    void step_started(int row);
    void step_progress(int row, const QString& detail);
    void step_finished(int row, bool ok, const QString& detail);

    void finished(bool all_ok);

private:
    void subscribe_to_agent_events();
    void start_stage(int index);
    void on_stage_step_finished();
    void finish();

    [[nodiscard]] bool dispatch_step(const SetupStep& step, int row);

    link::AgentRpcClient* rpc_;
    link::EventSubscription progress_sub_;
    link::EventSubscription done_sub_;

    QVector<SetupStage> stages_;
    int current_stage_ = -1;
    int stage_pending_ = 0;
    QHash<QString, int> job_to_row_;
    bool any_failure_ = false;
};

}
