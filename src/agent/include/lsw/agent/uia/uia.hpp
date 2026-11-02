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

class Uia final : public rpc::EventSource {
public:
    Uia();
    ~Uia() override;

    Uia(const Uia&) = delete;
    Uia& operator=(const Uia&) = delete;

    void shutdown();

    [[nodiscard]] WindowState get_window_state(std::uintptr_t hwnd);
    [[nodiscard]] bool invoke_by_selector(std::uintptr_t hwnd, const std::string& selector);
    [[nodiscard]] bool set_value(std::uintptr_t hwnd, const std::string& selector,
                                 const std::string& value);
    [[nodiscard]] std::string get_text(std::uintptr_t hwnd, int max_chars);
    [[nodiscard]] nlohmann::json dump_tree(std::uintptr_t hwnd, int max_depth);

    [[nodiscard]] std::uint64_t subscribe_pid_lifecycle(std::uint32_t pid);
    [[nodiscard]] std::uint64_t subscribe_pid_focus(std::uint32_t pid);
    [[nodiscard]] std::uint64_t subscribe_window_properties(std::uintptr_t hwnd);
    void unsubscribe(std::uint64_t subscription_id);

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    UiaRoot root_;
    UiaEventQueue queue_;
    std::unique_ptr<SubscriptionRegistry> subscriptions_;
};

}
