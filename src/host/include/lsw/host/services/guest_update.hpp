#pragma once

#include "lsw/host/services/guest_bundle.hpp"
#include "lsw/host/services/guest_update_status.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <QVector>

#include <atomic>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::config { class Settings; }
namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {

class StatusHub;

class GuestUpdate : public QObject {
    Q_OBJECT
public:
    GuestUpdate(config::Settings& settings, StatusHub& status, link::AgentRpcClient& rpc,
                QObject* parent = nullptr);
    ~GuestUpdate() override;

    [[nodiscard]] virtual bool running() const noexcept { return running_; }

    void run(bool force);

    virtual void run_if_stale(const QString& guest_release, const QString& guest_config,
                              const QString& guest_version);

    void retry();

    void note_last(const QVector<GuestUpdateStatus::GuestStep>& steps, const QString& finished,
                   bool ok);

    [[nodiscard]] static bool ok_of(const nlohmann::json& last);

    [[nodiscard]] static QVector<GuestUpdateStatus::GuestStep> steps_of(const nlohmann::json& last);
    [[nodiscard]] static QString finished_of(const nlohmann::json& last);
    [[nodiscard]] static bool restart_of(const nlohmann::json& last);
    [[nodiscard]] static QStringList reasons_of(const nlohmann::json& last);

Q_SIGNALS:
    void succeeded();
    void failed(const QString& message);

private:
    struct Outcome {
        bool ok = false;
        QString message;
        QVector<GuestUpdateStatus::GuestStep> steps;
        bool shipped = false;
        QString finished;
        bool last_ok = false;
        bool restart = false;
        QStringList reasons;
    };

    void begin(bool force, bool announce, const QString& current, const QString& current_config,
               const QString& current_version);
    void on_built(const std::shared_ptr<const GuestBundle::Contents>& contents, bool force,
                  const QString& current, const QString& current_config);
    void reset_live();
    void on_progress(const QString& step, const QString& status);
    void say_restart(const QStringList& reasons);
    void settle_live(const Outcome& outcome);
    [[nodiscard]] Outcome carry_out(const std::shared_ptr<const GuestBundle::Contents>& contents,
                                    bool force);
    void say_step(const QString& text);
    void publish(GuestUpdateStatus::State state, const QString& step, const QString& message);
    void finish(const Outcome& outcome);

    StatusHub& status_;
    link::AgentRpcClient& rpc_;
    GuestBundle bundle_;
    QFutureWatcher<Outcome> watcher_;
    GuestUpdateStatus snapshot_;
    bool running_ = false;
    bool last_force_ = false;
    std::atomic<bool> cancel_{false};
};

}
