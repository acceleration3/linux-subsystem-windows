#include "lsw/host/provision/setup_stage_runner.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/byte_size.hpp"
#include "lsw/host/provision/setup_plan_parser.hpp"
#include "setup_steps_template.hpp"

#include <QMetaObject>
#include <QTimer>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::provision {

namespace {

// Build the RPC frame for one step. The verb and its parameters are the
// only thing that differs between step kinds.
[[nodiscard]] nlohmann::json request_for(const SetupStep& step) {
    nlohmann::json params = nlohmann::json::object();
    params["name"] = step.name.toStdString();
    nlohmann::json request = nlohmann::json::object();
    switch (step.kind) {
    case StepKind::DownloadInstall:
        request["cmd"] = linkproto::cmd::kDownloadInstall;
        params["url"] = step.url.toStdString();
        params["args"] = step.args.toStdString();
        break;
    case StepKind::RunScript:
        request["cmd"] = linkproto::cmd::kRunScript;
        params["script"] = step.script.toStdString();
        break;
    case StepKind::DownloadExtract:
        request["cmd"] = linkproto::cmd::kDownloadExtract;
        params["url"] = step.url.toStdString();
        params["extract_to"] = step.extract_to.toStdString();
        params["script"] = step.script.toStdString();
        break;
    }
    request["params"] = std::move(params);
    return request;
}

}  // namespace

SetupStageRunner::SetupStageRunner(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(&rpc) {}

SetupStageRunner::~SetupStageRunner() { detach(); }

void SetupStageRunner::detach() {
    if (rpc_ == nullptr) {
        return;
    }
    if (progress_token_ != 0) {
        rpc_->off_event(progress_token_);
        progress_token_ = 0;
    }
    if (done_token_ != 0) {
        rpc_->off_event(done_token_);
        done_token_ = 0;
    }
    rpc_ = nullptr;
}

void SetupStageRunner::run() {
    QString error;
    const QByteArray plan_json = QByteArray::fromRawData(
        lsw::embedded::kSetupStepsTemplate,
        static_cast<int>(sizeof(lsw::embedded::kSetupStepsTemplate) - 1));
    stages_ = SetupPlanParser::parse(plan_json, &error);
    Q_EMIT plan_ready(stages_);

    if (stages_.isEmpty()) {
        LSW_WARN("provision: no setup steps configured ({})", error.toStdString());
        Q_EMIT finished(true);
        return;
    }
    subscribe_to_agent_events();
    // Defer one turn so a UI page connected to ``plan_ready`` has laid
    // out its rows before the first ``step_started`` arrives.
    QTimer::singleShot(0, this, [this] { start_stage(0); });
}

void SetupStageRunner::subscribe_to_agent_events() {
    // Handlers run on the channel's reader thread; everything they touch
    // is marshalled back here first.
    progress_token_ = rpc_->on_event(QString::fromLatin1(linkproto::topic::kSetupProgress),
                                     [this](const nlohmann::json& frame) {
        const auto job = QString::fromStdString(frame.value("job_id", std::string{}));
        const auto message = QString::fromStdString(frame.value("message", std::string{}));
        const auto bytes = frame.value("bytes", std::uint64_t{0});
        const auto total = frame.value("total_bytes", std::uint64_t{0});
        QMetaObject::invokeMethod(this, [this, job, message, bytes, total]() {
            const auto it = job_to_row_.constFind(job);
            if (it == job_to_row_.constEnd()) {
                return;
            }
            QString status;
            if (!message.isEmpty()) {
                LSW_INFO("provision: setup_progress job={} row={} '{}'",
                         job.toStdString(), it.value(), message.toStdString());
                status = message;
            } else if (total > 0) {
                status = QStringLiteral("%1% (%2 / %3)")
                             .arg((bytes * 100) / total)
                             .arg(platform::ByteSize::human(bytes),
                                  platform::ByteSize::human(total));
            } else if (bytes > 0) {
                status = platform::ByteSize::human(bytes);
            } else {
                status = QStringLiteral("Running...");
            }
            Q_EMIT step_progress(it.value(), status);
        }, Qt::QueuedConnection);
    });

    done_token_ = rpc_->on_event(QString::fromLatin1(linkproto::topic::kSetupDone),
                                 [this](const nlohmann::json& frame) {
        const auto job = QString::fromStdString(frame.value("job_id", std::string{}));
        const auto ok = frame.value("ok", false);
        const auto code = frame.value("exit_code", 0);
        const auto message = QString::fromStdString(frame.value("message", std::string{}));
        QMetaObject::invokeMethod(this, [this, job, ok, code, message]() {
            LSW_INFO("provision: setup_done job={} ok={} exit={} msg='{}'",
                     job.toStdString(), ok, code, message.toStdString());
            const auto it = job_to_row_.constFind(job);
            if (it == job_to_row_.constEnd()) {
                LSW_WARN("provision: setup_done for unknown job={} (already handled "
                         "or never dispatched)", job.toStdString());
                return;
            }
            const int row = it.value();
            const QString detail =
                ok ? (code == 0 ? QStringLiteral("Done")
                                : QStringLiteral("Done (exit %1)").arg(code))
                   : (message.isEmpty() ? QStringLiteral("exit %1").arg(code) : message);
            if (!ok) {
                any_failure_ = true;
            }
            Q_EMIT step_finished(row, ok, detail);
            job_to_row_.remove(job);
            on_stage_step_finished();
        }, Qt::QueuedConnection);
    });
}

bool SetupStageRunner::dispatch_step(const SetupStep& step, int row) {
    const auto request = request_for(step);
    LSW_INFO("provision: dispatch setup step row={} kind={} name='{}'", row,
             request["cmd"].get<std::string>(), step.name.toStdString());
    Q_EMIT step_started(row);

    // Blocking, but only until the agent acks with a job id — the work
    // itself runs in the guest and streams back as events. On a healthy
    // channel the ack is immediate, so the GUI thread is fine here.
    const auto reply = rpc_->call(request);
    if (reply.isEmpty()) {
        any_failure_ = true;
        Q_EMIT step_finished(row, false, QStringLiteral("RPC failed (no reply)"));
        return false;
    }
    try {
        const auto json = nlohmann::json::parse(reply.constData(),
                                                reply.constData() + reply.size());
        if (!json.value("ok", false)) {
            any_failure_ = true;
            Q_EMIT step_finished(row, false,
                                 QString::fromStdString(
                                     json.value("error", std::string{"agent error"})));
            return false;
        }
        const auto job_id = QString::fromStdString(json.value("job_id", std::string{}));
        if (job_id.isEmpty()) {
            any_failure_ = true;
            Q_EMIT step_finished(row, false,
                                 QStringLiteral("agent returned no job_id"));
            return false;
        }
        job_to_row_.insert(job_id, row);
        LSW_INFO("provision: setup step row={} accepted by agent job_id={}", row,
                 job_id.toStdString());
        return true;
    } catch (const nlohmann::json::exception& e) {
        LSW_WARN("provision: malformed setup ack: {}", e.what());
        any_failure_ = true;
        Q_EMIT step_finished(row, false, QStringLiteral("malformed ack"));
        return false;
    }
}

void SetupStageRunner::start_stage(int index) {
    if (index < 0 || index >= stages_.size()) {
        return;
    }
    current_stage_ = index;
    const auto& stage = stages_[index];

    // Rows are laid out in stage order with no gaps, so a stage's first
    // row is the running total of the steps before it.
    int row_offset = 0;
    for (int i = 0; i < index; ++i) {
        row_offset += static_cast<int>(stages_[i].steps.size());
    }

    stage_pending_ = static_cast<int>(stage.steps.size());
    LSW_INFO("provision: starting setup stage {}/{} ({} step(s), row_offset={})",
             index + 1, stages_.size(), stage_pending_, row_offset);

    // A step whose dispatch failed will never produce a setup_done, so
    // it must not be counted as pending or the stage never completes.
    int failed_to_dispatch = 0;
    for (int i = 0; i < stage.steps.size(); ++i) {
        if (!dispatch_step(stage.steps[i], row_offset + i)) {
            ++failed_to_dispatch;
        }
    }
    stage_pending_ -= failed_to_dispatch;

    if (stage_pending_ == 0) {
        QTimer::singleShot(0, this, [this, index] {
            const int next = index + 1;
            if (next >= stages_.size()) {
                finish();
                return;
            }
            start_stage(next);
        });
    }
}

void SetupStageRunner::on_stage_step_finished() {
    if (stage_pending_ <= 0) {
        return;
    }
    --stage_pending_;
    LSW_INFO("provision: setup stage {} step finished, {} still pending",
             current_stage_ + 1, stage_pending_);
    if (stage_pending_ > 0) {
        return;
    }
    const int next = current_stage_ + 1;
    if (next >= stages_.size()) {
        finish();
        return;
    }
    start_stage(next);
}

void SetupStageRunner::finish() { Q_EMIT finished(!any_failure_); }

}  // namespace lsw::host::provision
