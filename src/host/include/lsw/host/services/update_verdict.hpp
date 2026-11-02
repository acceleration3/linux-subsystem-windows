#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace lsw::host::link { struct AgentReply; }

namespace lsw::host::services {

// What one system_info reply says about an update run in progress
// (docs/guest-update.md, "The run", step 3), as plain inputs and an answer, so
// that the rules are the same for the worker that polls and for the tests.
class UpdateVerdict {
public:
    enum class Kind {
        Waiting,         // nothing of this run yet (or no answer): keep polling
        Failed,          // the guest reports this run failed: ``message`` says why
        RestartPending,  // this run is done and recorded, but Windows is yet to restart
        Succeeded,       // the guest has the shipped release
    };

    Kind kind = Kind::Waiting;
    // Failed: the guest's own message ("-" when it gave none).
    std::string message;
    // The last_update object the verdict was made from; the guest's steps are in it.
    nlohmann::json last;

    // The ``finished`` stamp of the last_update ``body`` (a system_info
    // result) carries; empty when there is none.
    [[nodiscard]] static std::string finished_of(const nlohmann::json& body);

    // Whether ``last`` is a result of this run rather than of an earlier
    // one: it has a ``finished`` that differs from ``baseline``, the one
    // system_info gave before the run. The guest's clock is never compared
    // with ours.
    [[nodiscard]] static bool is_mine(const nlohmann::json& last, const std::string& baseline);

    // The send phase is over early when the agent stops answering (the task
    // stops it) or a result of this run is already there.
    [[nodiscard]] static bool moved_on(const link::AgentReply& info, const std::string& baseline);

    // Whether the agent answering ``info`` started more than a second after
    // ``finished`` (the update result's UTC "yyyy-MM-ddTHH:mm:ss.fffZ"): one
    // of a boot that came after the update. agent_started is the process's
    // creation time as a Windows FILETIME, 100 ns ticks since 1601, on the
    // same guest clock.
    [[nodiscard]] static bool started_after(const link::AgentReply& info, const std::string& finished);

    // The decision, for one reply to system_info: ``baseline`` is the
    // ``finished`` before the run, ``shipped`` the release this manager ships.
    [[nodiscard]] static UpdateVerdict judge(const link::AgentReply& info, const std::string& baseline,
                                             const std::string& shipped);
};

}  // namespace lsw::host::services
