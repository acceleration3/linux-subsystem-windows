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

// Brings what LSW put into Windows to the release this manager ships
// (docs/guest-update.md, "The run", "On the host").
//
// A run is: build the bundle (GuestBundle), serve it to the guest from a
// GuestFileServer, send one run_script that downloads it into a staging
// directory and starts the one-shot task ``lsw-update`` there, then wait
// for the agent to answer again with the new release, up to 15 minutes. The
// task, not the agent, does the work, because the work stops the agent; so
// the agent being away in the meantime is expected, and the guest may
// restart. One run at a time.
//
// Its state goes to StatusHub (GuestUpdateStatus) as it changes, which is
// what the interface shows; the signals are for the services that go on
// from its end (GuestUpkeep).
class GuestUpdate : public QObject {
    Q_OBJECT
public:
    GuestUpdate(config::Settings& settings, StatusHub& status, link::AgentRpcClient& rpc,
                QObject* parent = nullptr);
    ~GuestUpdate() override;

    // Virtual so that GuestUpkeep can be tested against a stand-in.
    [[nodiscard]] virtual bool running() const noexcept { return running_; }

    // Update the guest now. ``force`` replaces every file and runs every
    // step whatever the guest has. Ignored while a run is going.
    void run(bool force);

    // Update the guest unless ``guest_release``, what its agent reports, is
    // the one this manager ships, in which case ``succeeded`` follows
    // without a run. Not forced.
    virtual void run_if_stale(const QString& guest_release, const QString& guest_version);

    // Run again as the last run was (forced or not), for "Try again" after
    // a failure. Ignored while a run is going.
    void retry();

Q_SIGNALS:
    // The guest has the shipped release.
    void succeeded();
    // ``message`` says why, with where the guest's log is.
    void failed(const QString& message);

private:
    struct Outcome {
        bool ok = false;
        QString message;
        // The guest's steps of this run, when it said.
        QVector<GuestUpdateStatus::GuestStep> steps;
        // A run shipped something, as against finding the guest current.
        bool shipped = false;
    };

    // ``current`` is the release the guest is known to have, if any: a bundle
    // that makes the same needs no run.
    // ``announce`` publishes "Preparing files" at once.
    void begin(bool force, bool announce, const QString& current, const QString& current_version);
    void on_built(const std::shared_ptr<const GuestBundle::Contents>& contents, bool force,
                  const QString& current);
    // The worker's half: serve, send, wait. Blocking.
    [[nodiscard]] Outcome carry_out(const std::shared_ptr<const GuestBundle::Contents>& contents,
                                    bool force);
    // The steps array of the guest's update-result.json.
    [[nodiscard]] static QVector<GuestUpdateStatus::GuestStep> steps_of(const nlohmann::json& last);
    // The step the host is on, from the worker.
    void say_step(const QString& text);
    void publish(GuestUpdateStatus::State state, const QString& step, const QString& message);
    void finish(const Outcome& outcome);

    StatusHub& status_;
    link::AgentRpcClient& rpc_;
    GuestBundle bundle_;
    QFutureWatcher<Outcome> watcher_;
    GuestUpdateStatus snapshot_;
    bool running_ = false;
    // What the last run was begun with, for retry().
    bool last_force_ = false;
    // Set when the machine stops during a run, or the object goes: the
    // worker gives up.
    std::atomic<bool> cancel_{false};
};

}  // namespace lsw::host::services
