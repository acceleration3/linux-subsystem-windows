#include "lsw/agent/uia/subscription_registry.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/uia/element_locator.hpp"
#include "lsw/agent/uia/focus_event_handler.hpp"
#include "lsw/agent/uia/lifecycle_event_handler.hpp"
#include "lsw/agent/uia/property_event_handler.hpp"
#include "lsw/agent/uia/uia_event_queue.hpp"

#include <iterator>
#include <utility>

namespace lsw::agent::uia {

std::uint64_t SubscriptionRegistry::store(Subscription sub) {
    const std::lock_guard<std::mutex> lock(mu_);
    const std::uint64_t id = next_id_++;
    subs_.emplace(id, std::move(sub));
    return id;
}

std::uint64_t SubscriptionRegistry::subscribe_pid_lifecycle(std::uint32_t pid) {
    if (root_ == nullptr) {
        return 0;
    }
    win::ComPtr<IUIAutomationElement> desktop;
    if (FAILED(root_->GetRootElement(desktop.GetAddressOf())) || !desktop) {
        return 0;
    }
    const auto handler =
        win::make_com_object<LifecycleEventHandler>(queue_, static_cast<DWORD>(pid));

    // Subtree scope catches the app's root window AND any dialogs or
    // popups it spawns later; the handler filters by PID.
    const HRESULT opened = root_->AddAutomationEventHandler(
        UIA_Window_WindowOpenedEventId, desktop.Get(), TreeScope_Subtree, nullptr, handler.Get());
    const HRESULT closed = root_->AddAutomationEventHandler(
        UIA_Window_WindowClosedEventId, desktop.Get(), TreeScope_Subtree, nullptr, handler.Get());
    if (FAILED(opened) || FAILED(closed)) {
        // Roll back whichever half succeeded so we do not leave a live
        // handler with no id to detach it by.
        if (SUCCEEDED(opened)) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, desktop.Get(),
                                                handler.Get());
        }
        if (SUCCEEDED(closed)) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, desktop.Get(),
                                                handler.Get());
        }
        LSW_AGENT_WARN("uia: AddAutomationEventHandler failed");
        return 0;
    }
    return store({SubscriptionKind::Lifecycle, handler, desktop});
}

std::uint64_t SubscriptionRegistry::subscribe_pid_focus(std::uint32_t pid) {
    if (root_ == nullptr) {
        return 0;
    }
    const auto handler = win::make_com_object<FocusEventHandler>(queue_, static_cast<DWORD>(pid));
    if (FAILED(root_->AddFocusChangedEventHandler(nullptr, handler.Get()))) {
        return 0;
    }
    // Focus handlers are process-wide, so there is no scope element.
    return store({SubscriptionKind::Focus, handler, nullptr});
}

std::uint64_t SubscriptionRegistry::subscribe_window_properties(std::uintptr_t hwnd) {
    if (root_ == nullptr) {
        return 0;
    }
    const auto element = ElementLocator::from_window(root_, hwnd);
    if (!element) {
        return 0;
    }
    const auto handler = win::make_com_object<PropertyEventHandler>(queue_);
    PROPERTYID properties[] = {UIA_WindowWindowVisualStatePropertyId, UIA_NamePropertyId,
                               UIA_WindowIsModalPropertyId};
    const HRESULT hr = root_->AddPropertyChangedEventHandlerNativeArray(
        element.Get(), TreeScope_Element, nullptr, handler.Get(), properties,
        static_cast<int>(std::size(properties)));
    if (FAILED(hr)) {
        return 0;
    }
    return store({SubscriptionKind::Property, handler, element});
}

void SubscriptionRegistry::unsubscribe(std::uint64_t id) {
    if (id == 0 || root_ == nullptr) {
        return;
    }
    Subscription sub;
    {
        const std::lock_guard<std::mutex> lock(mu_);
        const auto it = subs_.find(id);
        if (it == subs_.end()) {
            return;
        }
        sub = std::move(it->second);
        subs_.erase(it);
    }
    detach(sub);
}

void SubscriptionRegistry::clear() {
    std::unordered_map<std::uint64_t, Subscription> all;
    {
        const std::lock_guard<std::mutex> lock(mu_);
        all.swap(subs_);
    }
    if (root_ == nullptr) {
        return;
    }
    for (auto& [id, sub] : all) {
        detach(sub);
    }
}

void SubscriptionRegistry::detach(Subscription& sub) {
    if (!sub.handler) {
        return;
    }
    // Each kind detaches differently — this asymmetry is why Subscription
    // carries a kind tag rather than a single polymorphic handle.
    switch (sub.kind) {
    case SubscriptionKind::Lifecycle: {
        win::ComPtr<IUIAutomationEventHandler> handler;
        if (sub.scope && SUCCEEDED(sub.handler.As(&handler))) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, sub.scope.Get(),
                                                handler.Get());
            root_->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, sub.scope.Get(),
                                                handler.Get());
        }
        break;
    }
    case SubscriptionKind::Focus: {
        win::ComPtr<IUIAutomationFocusChangedEventHandler> handler;
        if (SUCCEEDED(sub.handler.As(&handler))) {
            root_->RemoveFocusChangedEventHandler(handler.Get());
        }
        break;
    }
    case SubscriptionKind::Property: {
        win::ComPtr<IUIAutomationPropertyChangedEventHandler> handler;
        if (sub.scope && SUCCEEDED(sub.handler.As(&handler))) {
            root_->RemovePropertyChangedEventHandler(sub.scope.Get(), handler.Get());
        }
        break;
    }
    }
    // Scope before handler, the order the hand-written releases used.
    sub.scope.Reset();
    sub.handler.Reset();
}

}  // namespace lsw::agent::uia
