// The run's decision on each system_info reply while a guest update is going
// (UpdateVerdict): which results are this run's, when the run has succeeded,
// failed, or is waiting for the restart it asked for, and the FILETIME
// arithmetic that tells a boot after the update from one before it.

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/services/update_verdict.hpp"

#include <QDateTime>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>

using lsw::host::link::AgentReply;
using lsw::host::services::UpdateVerdict;
using Kind = UpdateVerdict::Kind;
using nlohmann::json;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

const std::string kShipped = "aa11";
const std::string kBaseline = "2026-11-02T10:00:00.000Z";
const std::string kFinished = "2026-11-02T10:15:30.123Z";

AgentReply reply(json body) {
    AgentReply r;
    r.ok = true;
    r.body = std::move(body);
    return r;
}

json last(bool ok, const std::string& finished, bool restart = false, const char* message = nullptr) {
    json l = {{"ok", ok}, {"finished", finished}, {"restart", restart}};
    if (message != nullptr) {
        l["message"] = message;
    }
    return l;
}

// FILETIME ticks of ``finished`` plus ``offset_ms``.
std::uint64_t filetime_of(const std::string& finished, qint64 offset_ms) {
    const QDateTime at = QDateTime::fromString(QString::fromStdString(finished), Qt::ISODateWithMs);
    return static_cast<std::uint64_t>((at.toMSecsSinceEpoch() + offset_ms + 11'644'473'600'000) * 10'000);
}

}  // namespace

int main() {
    // A forced run on a guest that already has the shipped release.
    check(UpdateVerdict::judge(reply({{"release", kShipped}, {"last_update", last(true, kBaseline)}}),
                               kBaseline, kShipped).kind == Kind::Waiting,
          "a forced run is not done on the result and release from before it");
    check(UpdateVerdict::judge(reply({{"release", kShipped}}), kBaseline, kShipped).kind == Kind::Waiting,
          "no last_update at all keeps waiting");
    check(UpdateVerdict::judge(reply({{"release", kShipped}, {"last_update", last(true, kFinished)}}),
                               kBaseline, kShipped).kind == Kind::Succeeded,
          "a forced run succeeds on an ok result of its own with the shipped release");
    check(UpdateVerdict::judge(reply({{"release", kShipped}, {"last_update", last(true, kFinished)}}),
                               "", kShipped).kind == Kind::Succeeded,
          "a guest that had no result before takes the first as this run's");

    // Failures.
    check(UpdateVerdict::judge(reply({{"release", "old"}, {"last_update", last(false, kBaseline, false, "earlier")}}),
                               kBaseline, kShipped).kind == Kind::Waiting,
          "a failed result of an earlier run (same finished as the baseline) is ignored");
    {
        const auto v = UpdateVerdict::judge(
            reply({{"release", "old"}, {"last_update", last(false, kFinished, false, "disk full")}}),
            kBaseline, kShipped);
        check(v.kind == Kind::Failed && v.message == "disk full", "a failed result of this run is a failure with its message");
        check(v.last.value("finished", "") == kFinished, "the failure carries the result for its steps");
    }
    check(UpdateVerdict::judge(reply({{"release", "old"}, {"last_update", last(false, kFinished)}}), kBaseline,
                               kShipped).message == "-",
          "a failure without a message says '-'");
    check(UpdateVerdict::judge(reply({{"release", kShipped}, {"last_update", last(false, kFinished, true, "x")}}),
                               kBaseline, kShipped).kind == Kind::Failed,
          "a failure is checked before the release: the shipped release does not make it a success");

    // Release.
    check(UpdateVerdict::judge(reply({{"release", "other"}, {"last_update", last(true, kFinished)}}), kBaseline,
                               kShipped).kind == Kind::Waiting,
          "an ok result of this run with another release keeps waiting");
    check(UpdateVerdict::judge(reply({{"last_update", last(true, kFinished)}}), kBaseline, kShipped).kind
              == Kind::Waiting,
          "an ok result with no release reported keeps waiting");

    // Restart.
    {
        const json l = last(true, kFinished, true);
        json before = {{"release", kShipped}, {"last_update", l}, {"agent_started", filetime_of(kFinished, -5000)}};
        check(UpdateVerdict::judge(reply(before), kBaseline, kShipped).kind == Kind::RestartPending,
              "restart:true with an agent started before the result waits");
        before["agent_started"] = filetime_of(kFinished, 1000);
        check(UpdateVerdict::judge(reply(before), kBaseline, kShipped).kind == Kind::RestartPending,
              "restart:true with an agent started exactly 1 s after the result still waits");
        before["agent_started"] = filetime_of(kFinished, 1000) + 1;
        check(UpdateVerdict::judge(reply(before), kBaseline, kShipped).kind == Kind::Succeeded,
              "restart:true with an agent started more than 1 s after the result succeeds");
        before.erase("agent_started");
        check(UpdateVerdict::judge(reply(before), kBaseline, kShipped).kind == Kind::RestartPending,
              "restart:true with no agent_started waits");
        json no_restart = {{"release", kShipped}, {"last_update", last(true, kFinished, false)},
                           {"agent_started", filetime_of(kFinished, -5000)}};
        check(UpdateVerdict::judge(reply(no_restart), kBaseline, kShipped).kind == Kind::Succeeded,
              "restart:false succeeds at once whatever the agent's start");
    }

    // No answer.
    {
        AgentReply none = AgentReply::failure("timed out");
        check(UpdateVerdict::judge(none, kBaseline, kShipped).kind == Kind::Waiting,
              "an unanswered system_info keeps waiting");
        none.body = {{"release", kShipped}, {"last_update", last(true, kFinished)}};
        check(UpdateVerdict::judge(none, kBaseline, kShipped).kind == Kind::Waiting,
              "an unanswered system_info keeps waiting whatever its body holds");
        check(UpdateVerdict::moved_on(none, kBaseline), "the send phase gives way when the agent stops answering");
    }

    // Own result and baseline.
    check(UpdateVerdict::finished_of(json{{"last_update", last(true, kBaseline)}}) == kBaseline,
          "the baseline is the last_update's finished");
    check(UpdateVerdict::finished_of(json{}).empty() && UpdateVerdict::finished_of(json{{"last_update", 5}}).empty()
              && UpdateVerdict::finished_of(json{{"last_update", json::object()}}).empty(),
          "no last_update, or one that is no object, has no baseline");
    check(!UpdateVerdict::is_mine(json::object(), "") && !UpdateVerdict::is_mine(json(nullptr), "")
              && !UpdateVerdict::is_mine(json{{"ok", true}}, kBaseline),
          "a result without a finished stamp is nobody's");
    check(UpdateVerdict::is_mine(last(true, kFinished), kBaseline) && !UpdateVerdict::is_mine(last(true, kBaseline), kBaseline),
          "a result is this run's when its finished differs from the baseline");
    check(!UpdateVerdict::moved_on(reply({{"last_update", last(true, kBaseline)}}), kBaseline)
              && UpdateVerdict::moved_on(reply({{"last_update", last(false, kFinished)}}), kBaseline),
          "the send phase gives way on a result of this run, not on an old one");

    // started_after.
    {
        const AgentReply at = reply({{"agent_started", filetime_of(kFinished, 1000)}});
        const AgentReply after = reply({{"agent_started", filetime_of(kFinished, 1000) + 1}});
        const AgentReply before = reply({{"agent_started", filetime_of(kFinished, 1000) - 1}});
        check(!UpdateVerdict::started_after(at, kFinished), "started_after: exactly +1 s is not after");
        check(UpdateVerdict::started_after(after, kFinished), "started_after: one tick past +1 s is after");
        check(!UpdateVerdict::started_after(before, kFinished), "started_after: one tick short of +1 s is not after");
        check(UpdateVerdict::started_after(reply({{"agent_started", filetime_of("2026-11-02T10:15:30.124Z", 1000) + 1}}),
                                           "2026-11-02T10:15:30.124Z"),
              "started_after: the milliseconds of finished are read");
        check(!UpdateVerdict::started_after(reply({{"agent_started", filetime_of("2026-11-02T10:15:30.123Z", 1000) + 5000}}),
                                            "2026-11-02T10:15:30.999Z"),
              "started_after: .999 is not .123 (a later finished moves the boundary)");
        check(!UpdateVerdict::started_after(after, "") && !UpdateVerdict::started_after(after, "not a date"),
              "started_after: an empty or invalid finished is false");
        check(!UpdateVerdict::started_after(reply({{"agent_started", 0}}), kFinished)
                  && !UpdateVerdict::started_after(reply(json::object()), kFinished),
              "started_after: a zero or missing agent_started is false");
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
