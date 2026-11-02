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

    virtual void run_if_stale(const QString& guest_release, const QString& guest_version);

    void retry();

Q_SIGNALS:
    void succeeded();
    void failed(const QString& message);

private:
    struct Outcome {
        bool ok = false;
        QString message;
        QVector<GuestUpdateStatus::GuestStep> steps;
        bool shipped = false;
    };

    void begin(bool force, bool announce, const QString& current, const QString& current_version);
    void on_built(const std::shared_ptr<const GuestBundle::Contents>& contents, bool force,
                  const QString& current);
    [[nodiscard]] Outcome carry_out(const std::shared_ptr<const GuestBundle::Contents>& contents,
                                    bool force);
    [[nodiscard]] static QVector<GuestUpdateStatus::GuestStep> steps_of(const nlohmann::json& last);
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
