#include "lsw/host/services/guest_update.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/link/event_subscription.hpp"
#include "lsw/host/services/guest_file_server.hpp"
#include "lsw/host/services/job_end.hpp"
#include "lsw/host/services/update_batch.hpp"
#include "lsw/host/services/update_verdict.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::services {

namespace {
constexpr auto kRunWindow = std::chrono::minutes(15);
constexpr auto kPollInterval = std::chrono::seconds(2);
constexpr auto kSendWindow = std::chrono::minutes(10);

}

GuestUpdate::GuestUpdate(config::Settings& settings, StatusHub& status, link::AgentRpcClient& rpc,
                         QObject* parent)
    : QObject(parent), status_(status), rpc_(rpc), bundle_(settings, this) {
    connect(&watcher_, &QFutureWatcher<Outcome>::finished, this,
            [this] { finish(watcher_.result()); });
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Vm && running_ && !status_.vm().running()) {
            cancel_.store(true);
        }
    });
}

GuestUpdate::~GuestUpdate() {
    cancel_.store(true);
    watcher_.waitForFinished();
}

void GuestUpdate::run(bool force) {
    begin(force, true, snapshot_.guest_release, snapshot_.guest_version);
}

void GuestUpdate::run_if_stale(const QString& guest_release, const QString& guest_version) {
    begin(false, guest_release.isEmpty(), guest_release, guest_version);
}

void GuestUpdate::retry() { run(last_force_); }

void GuestUpdate::begin(bool force, bool announce, const QString& current,
                        const QString& current_version) {
    if (running_) {
        return;
    }
    running_ = true;
    LSW_DEBUG("update: checking (guest release {}{})", current.isEmpty() ? "unknown" : current.left(16).toStdString(),
              force ? ", forced" : "");
    last_force_ = force;
    cancel_.store(false);
    snapshot_.guest_release = current;
    snapshot_.guest_version = current_version;
    snapshot_.last_steps.clear();
    if (announce) {
        publish(GuestUpdateStatus::State::Running, QStringLiteral("Preparing files"), {});
    }

    QVector<GuestBundle::Mount> mounts;
    if (const vm::DomainDescription* definition = status_.vm().definition()) {
        mounts = GuestBundle::mounts_of(*definition);
    }
    auto* const once = new QObject(this);
    connect(&bundle_, &GuestBundle::built, once,
            [this, once, force, current](const std::shared_ptr<const GuestBundle::Contents>& c) {
                once->deleteLater();
                on_built(c, force, current);
            });
    bundle_.build(mounts);
}

void GuestUpdate::on_built(const std::shared_ptr<const GuestBundle::Contents>& contents,
                           bool force, const QString& current) {
    if (!contents->error.isEmpty()) {
        finish({false, QStringLiteral("LSW's files for Windows could not be put together: %1")
                           .arg(contents->error)});
        return;
    }
    snapshot_.shipped_release = QString::fromStdString(contents->release);
    snapshot_.shipped_version = QString::fromStdString(contents->version);
    if (!force && !current.isEmpty() && current == snapshot_.shipped_release) {
        LSW_INFO("update: the guest has the shipped release {}", contents->release.substr(0, 16));
        finish({true, {}, {}, false});
        return;
    }
    LSW_INFO("update: shipping release {} ({} files) to a guest at {}{}", contents->release.substr(0, 16),
             contents->files.size(), current.isEmpty() ? "-" : current.left(16).toStdString(),
             force ? ", forced" : "");
    publish(GuestUpdateStatus::State::Running, QStringLiteral("Sending files to Windows"), {});
    watcher_.setFuture(QtConcurrent::run([this, contents, force] { return carry_out(contents, force); }));
}

QVector<GuestUpdateStatus::GuestStep> GuestUpdate::steps_of(const nlohmann::json& last) {
    QVector<GuestUpdateStatus::GuestStep> out;
    const auto steps = last.is_object() ? last.value("steps", nlohmann::json{}) : nlohmann::json{};
    if (!steps.is_array()) {
        return out;
    }
    const auto text = [](const nlohmann::json& step, const char* key) {
        const auto value = step.value(key, nlohmann::json{});
        return value.is_string() ? QString::fromStdString(value.get<std::string>()) : QString{};
    };
    for (const auto& step : steps) {
        if (step.is_object()) {
            out.push_back({text(step, "name"), text(step, "status"), text(step, "message")});
        }
    }
    return out;
}

void GuestUpdate::say_step(const QString& text) {
    QPointer<GuestUpdate> self(this);
    QMetaObject::invokeMethod(this, [self, text] {
        if (self && self->running_) {
            self->publish(GuestUpdateStatus::State::Running, text, {});
        }
    }, Qt::QueuedConnection);
}

GuestUpdate::Outcome GuestUpdate::carry_out(const std::shared_ptr<const GuestBundle::Contents>& contents,
                                            bool force) {
    GuestFileServer::Files files;
    for (std::size_t i = 0; i < static_cast<std::size_t>(contents->files.size()); ++i) {
        files.emplace("f" + std::to_string(i), contents->files[static_cast<qsizetype>(i)].bytes);
    }
    files.emplace("manifest.txt", QByteArray::fromStdString(contents->manifest));
    files.emplace("shares.txt", QByteArray::fromStdString(contents->shares));
    GuestFileServer server{std::move(files)};
    if (!server.start()) {
        return {false, QStringLiteral("Could not serve LSW's files to Windows: %1")
                           .arg(QString::fromStdString(server.error()))};
    }

    LSW_DEBUG("update: serving {} files on port {}", contents->files.size() + 2, server.port());
    link::AgentReply before;
    for (int attempt = 0; attempt < 3 && !cancel_.load(); ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(kPollInterval);
        }
        before = rpc_.request(linkproto::cmd::kSystemInfo);
        if (before.ok) {
            break;
        }
    }
    if (cancel_.load()) {
        return {false, QStringLiteral("Windows stopped during the update.")};
    }
    if (!before.ok) {
        return {false, QStringLiteral("Windows did not answer before the update.")};
    }
    const std::string finished_before = UpdateVerdict::finished_of(before.body);

    JobEnd ends;
    const link::EventSubscription done_sub = rpc_.subscribe(
        linkproto::topic::kSetupDone, [&ends](const nlohmann::json& frame) { ends.on_done(frame); });
    const auto started = std::chrono::steady_clock::now();
    const auto ack = rpc_.request(
        linkproto::cmd::kRunScript,
        {{"name", "update LSW"}, {"script", UpdateBatch::send(server.port(), contents->release, *contents, force)}});
    const std::string job = ack.body.value("job_id", std::string{});
    if (!ack.ok || job.empty()) {
        return {false, QStringLiteral("The agent would not start the update: %1")
                           .arg(QString::fromStdString(ack.error))};
    }

    LSW_DEBUG("update: send job {} started", job);
    std::optional<JobEnd::End> end;
    bool moved_on = false;
    while (!end && !moved_on && !cancel_.load()
           && std::chrono::steady_clock::now() - started < kSendWindow) {
        end = ends.wait(job, std::chrono::seconds(1));
        if (end) {
            break;
        }
        const auto info = rpc_.request(linkproto::cmd::kSystemInfo);
        moved_on = UpdateVerdict::moved_on(info, finished_before);
    }
    if (cancel_.load()) {
        return {false, QStringLiteral("Windows stopped during the update.")};
    }
    if (!end && !moved_on) {
        return {false, UpdateBatch::with_log(QStringLiteral("Windows did not take LSW's files in ten minutes."))};
    }
    if (end) {
        LSW_INFO("update: send job {} ended ok={} exit={} ({} download(s) served)", job, end->ok,
                 end->exit_code, server.downloads_served());
        if (const auto why = UpdateBatch::send_failure(end->ok, end->exit_code, end->message)) {
            return {false, *why};
        }
    } else {
        LSW_INFO("update: send job {} not reported; the agent stopped or the result came", job);
    }

    say_step(QStringLiteral("Updating Windows"));
    bool restart_noted = false;
    while (!cancel_.load() && std::chrono::steady_clock::now() - started < kRunWindow) {
        std::this_thread::sleep_for(kPollInterval);
        const auto info = rpc_.request(linkproto::cmd::kSystemInfo);
        const auto verdict = UpdateVerdict::judge(info, finished_before, contents->release);
        switch (verdict.kind) {
        case UpdateVerdict::Kind::Waiting:
            break;
        case UpdateVerdict::Kind::Failed:
            return {false, UpdateBatch::with_log(QStringLiteral("Windows could not be updated: %1.")
                                                     .arg(QString::fromStdString(verdict.message))),
                    steps_of(verdict.last)};
        case UpdateVerdict::Kind::RestartPending:
            if (!restart_noted) {
                restart_noted = true;
                LSW_INFO("update: Windows is restarting to finish the update");
            }
            say_step(QStringLiteral("Restarting Windows"));
            break;
        case UpdateVerdict::Kind::Succeeded:
            LSW_DEBUG("update: the guest reports release {}", contents->release.substr(0, 16));
            return {true, {}, steps_of(verdict.last), true};
        }
    }
    if (cancel_.load()) {
        return {false, QStringLiteral("Windows stopped during the update.")};
    }

    LSW_INFO("update: no verdict in time; reading the result file");
    const auto read = rpc_.request(linkproto::cmd::kRunScript,
                                   {{"name", "read update result"},
                                    {"script", UpdateBatch::read_result(finished_before)}});
    const std::string read_job = read.body.value("job_id", std::string{});
    std::optional<JobEnd::End> result;
    if (read.ok && !read_job.empty()) {
        result = ends.wait(read_job, std::chrono::seconds(30));
    }
    return {false, UpdateBatch::unreported(result ? std::optional<int>{result->exit_code} : std::nullopt)};
}

void GuestUpdate::publish(GuestUpdateStatus::State state, const QString& step, const QString& message) {
    snapshot_.state = state;
    snapshot_.step = step;
    snapshot_.message = message;
    status_.set_update(snapshot_);
}

void GuestUpdate::finish(const Outcome& outcome) {
    running_ = false;
    if (outcome.ok) {
        if (outcome.shipped) {
            LSW_INFO("update: Windows has release {}", snapshot_.shipped_release.left(16).toStdString());
        }
        snapshot_.guest_release = snapshot_.shipped_release;
        snapshot_.guest_version = snapshot_.shipped_version;
        snapshot_.last_steps = outcome.steps;
        publish(GuestUpdateStatus::State::Succeeded, {}, {});
        Q_EMIT succeeded();
        return;
    }
    LSW_WARN("update: {}", outcome.message.toStdString());
    snapshot_.last_steps = outcome.steps;
    publish(GuestUpdateStatus::State::Failed, {}, outcome.message);
    Q_EMIT failed(outcome.message);
}

}
