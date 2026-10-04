#pragma once

#include "lsw/agent/rpc/event.hpp"
#include "lsw/agent/rpc/event_source.hpp"
#include "lsw/agent/uia/uia_event_queue.hpp"
#include "lsw/agent/uia/uia_root.hpp"
#include "lsw/agent/uia/window_state.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace lsw::agent::uia {

class SubscriptionRegistry;

// The agent's Microsoft UI Automation facade.
//
// Composition only: it owns the UIA root, the event queue that COM
// threads push into, and the subscription registry — and forwards each
// call to the query class that implements it. Handlers depend on this
// one type instead of a dozen, and it is the only file in the directory
// that deliberately avoids including <uiautomation.h>, so RPC handlers
// do not compile the whole UIA header set.
//
// Threading:
//   * Constructed on the agent's main thread, after the COM apartment.
//   * Queries run on the dispatcher thread; UIA marshals across.
//   * Event handlers fire on UIA's own thread pool and push into the
//     queue, which ``collect_pending`` drains on the transport tick.
class Uia final : public rpc::EventSource {
public:
    Uia();
    ~Uia() override;

    Uia(const Uia&) = delete;
    Uia& operator=(const Uia&) = delete;

    // Detach every subscription and release COM-held objects while the
    // apartment is still alive. Called by AgentServices before the
    // destructor chain runs.
    void shutdown();

    // On hosts where UIA is unavailable (very rare on Win10+) every
    // method below short-circuits to a benign no-op, so handlers stay
    // correct without checking.

    // ---- queries ---------------------------------------------------------

    [[nodiscard]] WindowState get_window_state(std::uintptr_t hwnd);
    [[nodiscard]] bool invoke_by_selector(std::uintptr_t hwnd, const std::string& selector);
    [[nodiscard]] bool set_value(std::uintptr_t hwnd, const std::string& selector,
                                 const std::string& value);
    [[nodiscard]] std::string get_text(std::uintptr_t hwnd, int max_chars);
    [[nodiscard]] nlohmann::json dump_tree(std::uintptr_t hwnd, int max_depth);

    // ---- subscriptions ---------------------------------------------------

    [[nodiscard]] std::uint64_t subscribe_pid_lifecycle(std::uint32_t pid);
    [[nodiscard]] std::uint64_t subscribe_pid_focus(std::uint32_t pid);
    [[nodiscard]] std::uint64_t subscribe_window_properties(std::uintptr_t hwnd);
    void unsubscribe(std::uint64_t subscription_id);

    // EventSource: drain what the COM threads have posted since the
    // last call.
    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    UiaRoot root_;
    UiaEventQueue queue_;
    // unique_ptr so this header need not define SubscriptionRegistry,
    // which pulls in <uiautomation.h>.
    std::unique_ptr<SubscriptionRegistry> subscriptions_;
};

}  // namespace lsw::agent::uia
