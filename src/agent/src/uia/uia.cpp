#include "lsw/agent/uia/uia.hpp"

#include "lsw/agent/uia/uia_window.hpp"

#include "lsw/agent/uia/element_invoker.hpp"
#include "lsw/agent/uia/element_value_setter.hpp"
#include "lsw/agent/uia/subscription_registry.hpp"
#include "lsw/agent/uia/text_extractor.hpp"
#include "lsw/agent/uia/tree_dumper.hpp"
#include "lsw/agent/uia/window_state_query.hpp"

namespace lsw::agent::uia {

Uia::Uia()
    : subscriptions_(std::make_unique<SubscriptionRegistry>(root_.get(), queue_)) {}

Uia::~Uia() = default;

void Uia::shutdown() { root_.shutdown(); }

bool Uia::ok() const noexcept { return root_.ok(); }

WindowState Uia::get_window_state(std::uintptr_t hwnd) {
    return WindowStateQuery::for_window(UiaWindow{root_.get(), hwnd});
}

bool Uia::invoke_by_selector(std::uintptr_t hwnd, const std::string& selector) {
    return ElementInvoker::invoke(UiaWindow{root_.get(), hwnd}, selector);
}

bool Uia::set_value(std::uintptr_t hwnd, const std::string& selector,
                    const std::string& value) {
    return ElementValueSetter::set_value(UiaWindow{root_.get(), hwnd}, selector, value);
}

std::string Uia::get_text(std::uintptr_t hwnd, int max_chars) {
    return TextExtractor::from_window(UiaWindow{root_.get(), hwnd}, max_chars);
}

nlohmann::json Uia::dump_tree(std::uintptr_t hwnd, int max_depth) {
    return TreeDumper::dump(UiaWindow{root_.get(), hwnd}, max_depth);
}

std::uint64_t Uia::subscribe_pid_lifecycle(std::uint32_t pid) {
    return subscriptions_->subscribe_pid_lifecycle(pid);
}

std::uint64_t Uia::subscribe_pid_focus(std::uint32_t pid) {
    return subscriptions_->subscribe_pid_focus(pid);
}

std::uint64_t Uia::subscribe_window_properties(std::uintptr_t hwnd) {
    return subscriptions_->subscribe_window_properties(hwnd);
}

void Uia::unsubscribe(std::uint64_t subscription_id) {
    subscriptions_->unsubscribe(subscription_id);
}

std::vector<rpc::Event> Uia::collect_pending() { return queue_.collect_pending(); }

}  // namespace lsw::agent::uia
