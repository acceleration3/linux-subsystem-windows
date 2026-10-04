// The host half of the agent channel, without an agent.
//
// What is under test is the bookkeeping that used to live inline in
// AgentRpcClient and could only be exercised with a VM on the other end:
// how a reply frame is judged a success or a failure, how a reply finds
// the caller waiting for it (and what happens when that caller has
// already given up), and how an event subscription goes away while the
// reader thread may be inside its handler.

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/event_bus.hpp"
#include "lsw/host/link/event_subscription.hpp"
#include "lsw/host/link/pending_calls.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace lsw::host::link;
using namespace std::chrono_literals;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void test_reply_decoding() {
    const auto ok = AgentReply::from_frame({{"id", 1}, {"result", {{"pid", 42}}}});
    check(ok.ok && ok.body.value("pid", 0) == 42, "a plain result is a success");

    const auto frame_error = AgentReply::from_frame({{"id", 1}, {"error", "unknown command: x"}});
    check(!frame_error.ok && frame_error.error == "unknown command: x",
          "an error frame is a failure carrying its message");

    const auto refused =
        AgentReply::from_frame({{"id", 1}, {"result", {{"ok", false}, {"error", "no such exe"}}}});
    check(!refused.ok && refused.error == "no such exe", "ok:false is a failure");
    check(refused.body.is_object(), "a refusal keeps its body");

    const auto bare_refusal = AgentReply::from_frame({{"id", 1}, {"result", {{"ok", false}}}});
    check(!bare_refusal.ok && !bare_refusal.error.empty(),
          "ok:false with no detail still says something");

    const auto result_error = AgentReply::from_frame(
        {{"id", 1}, {"result", {{"entries", nlohmann::json::array()}, {"error", "denied"}}}});
    check(!result_error.ok && result_error.error == "denied",
          "an error field inside the result is a failure");

    const auto empty_error =
        AgentReply::from_frame({{"id", 1}, {"result", {{"ok", true}, {"error", ""}}}});
    check(empty_error.ok, "an empty error field is not a failure");

    const auto non_object = AgentReply::from_frame({{"id", 1}, {"result", 7}});
    check(non_object.ok && non_object.body == 7, "a non-object result passes through");

    check(!AgentReply::from_frame({{"id", 1}}).ok, "neither result nor error is a failure");
    check(!AgentReply::from_frame(nlohmann::json::array()).ok, "a non-object frame is a failure");

    const auto odd_error = AgentReply::from_frame({{"id", 1}, {"error", {{"code", 5}}}});
    check(!odd_error.ok && !odd_error.error.empty(), "a non-string error does not throw");
}

void test_pending_calls() {
    PendingCalls calls;
    {
        PendingCalls::Ticket ticket{calls, 1};
        std::thread reader{[&] { calls.deliver(1, AgentReply::from_frame({{"result", 5}})); }};
        const auto reply = ticket.wait(2s);
        reader.join();
        check(reply.has_value() && reply->ok && reply->body == 5, "a reply reaches its ticket");
    }
    {
        PendingCalls::Ticket ticket{calls, 2};
        calls.deliver(3, AgentReply::from_frame({{"result", 1}}));
        check(!ticket.wait(20ms).has_value(), "a reply for another id does not wake a ticket");
    }
    {
        // The ticket is gone; a late reply must land nowhere.
        calls.deliver(2, AgentReply::from_frame({{"result", 1}}));
        check(true, "a reply after its ticket closed is dropped");
    }
    {
        PendingCalls::Ticket a{calls, 10};
        PendingCalls::Ticket b{calls, 11};
        calls.fail_all("connection dropped");
        const auto ra = a.wait(1s);
        const auto rb = b.wait(1s);
        check(ra && !ra->ok && ra->error == "connection dropped" && rb && !rb->ok,
              "fail_all releases every waiter with the reason");
    }
    {
        // Replies racing timeouts: nothing may write into a ticket that
        // has returned. Run under ASan to make this test bite.
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> next{100};
        std::thread reader{[&] {
            while (!stop.load()) {
                calls.deliver(next.load(), AgentReply::from_frame({{"result", 1}}));
            }
        }};
        for (int i = 0; i < 2000; ++i) {
            PendingCalls::Ticket ticket{calls, next.load()};
            (void)ticket.wait(0ms);
            next.fetch_add(1);
        }
        stop.store(true);
        reader.join();
        check(true, "replies racing timeouts never touch a closed ticket");
    }
}

void test_event_bus() {
    EventBus bus;
    std::vector<std::string> seen;
    auto first = bus.subscribe("log", [&](const nlohmann::json& f) {
        seen.push_back("first:" + f.value("msg", std::string{}));
    });
    auto second = bus.subscribe("log", [&](const nlohmann::json&) { seen.emplace_back("second"); });
    auto other = bus.subscribe("console_output", [&](const nlohmann::json&) {
        seen.emplace_back("other");
    });

    bus.dispatch({{"event", "log"}, {"msg", "hi"}});
    check(seen == std::vector<std::string>{"first:hi", "second"},
          "handlers of a topic run in subscription order, and only theirs");

    seen.clear();
    first.reset();
    bus.dispatch({{"event", "log"}});
    check(seen == std::vector<std::string>{"second"}, "a reset subscription stops receiving");

    seen.clear();
    {
        auto scoped = bus.subscribe("log", [&](const nlohmann::json&) { seen.emplace_back("scoped"); });
    }
    bus.dispatch({{"event", "log"}});
    check(seen == std::vector<std::string>{"second"}, "a destroyed subscription stops receiving");

    seen.clear();
    EventSubscription moved = std::move(second);
    bus.dispatch({{"event", "log"}});
    check(seen == std::vector<std::string>{"second"} && !second && moved,
          "a moved subscription keeps its handler");

    bus.dispatch({{"event", 5}});
    bus.dispatch({{"no_event", "log"}});
    bus.dispatch(nlohmann::json::array());
    check(true, "frames without a string topic are ignored");

    {
        EventSubscription self;
        int calls = 0;
        self = bus.subscribe("once", [&](const nlohmann::json&) {
            ++calls;
            self.reset();
        });
        bus.dispatch({{"event", "once"}});
        bus.dispatch({{"event", "once"}});
        check(calls == 1, "a handler can unsubscribe itself");
    }

    {
        // Removal waits for a handler already running on another thread.
        std::atomic<bool> entered{false};
        std::atomic<bool> finished{false};
        auto slow = bus.subscribe("slow", [&](const nlohmann::json&) {
            entered.store(true);
            std::this_thread::sleep_for(100ms);
            finished.store(true);
        });
        std::thread reader{[&] { bus.dispatch({{"event", "slow"}}); }};
        while (!entered.load()) {
            std::this_thread::yield();
        }
        slow.reset();
        check(finished.load(), "reset waits out a handler in progress");
        reader.join();
    }
}

}  // namespace

int main() {
    test_reply_decoding();
    test_pending_calls();
    test_event_bus();
    std::printf("%s\n", g_failures == 0 ? "all passed" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
