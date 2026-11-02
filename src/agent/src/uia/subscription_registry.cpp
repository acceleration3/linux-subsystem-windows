#include "lsw/agent/uia/subscription_registry.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/uia/element_locator.hpp"
#include "lsw/agent/uia/focus_event_handler.hpp"
#include "lsw/agent/uia/lifecycle_event_handler.hpp"
#include "lsw/agent/uia/property_event_handler.hpp"
#include "lsw/agent/uia/uia_event_queue.hpp"
#include "lsw/agent/win/error_text.hpp"

#include <iterator>
#include <utility>

namespace lsw::agent::uia {

std::uint64_t SubscriptionRegistry::store(Subscription sub) {
    const std::lock_guard<std::mutex> lock(mu_);
    const std::uint64_t id = next_id_++;
    subs_.emplace(id, std::move(sub));
    LSW_AGENT_DEBUG("uia: subscription {} stored", id);
    return id;
}

std::uint64_t SubscriptionRegistry::subscribe_pid_lifecycle(std::uint32_t pid) {
    if (root_ == nullptr) {
        return 0;
    }
    win::ComPtr<IUIAutomationElement> desktop;
    if (FAILED(root_->GetRootElement(desktop.GetAddressOf())) || !desktop) {
        LSW_AGENT_WARN("uia: the desktop element could not be read for pid {}", pid);
        return 0;
    }
    const auto handler =
        win::make_com_object<LifecycleEventHandler>(queue_, static_cast<DWORD>(pid));

    const HRESULT opened = root_->AddAutomationEventHandler(
        UIA_Window_WindowOpenedEventId, desktop.Get(), TreeScope_Subtree, nullptr, handler.Get());
    const HRESULT closed = root_->AddAutomationEventHandler(
        UIA_Window_WindowClosedEventId, desktop.Get(), TreeScope_Subtree, nullptr, handler.Get());
    if (FAILED(opened) || FAILED(closed)) {
        if (SUCCEEDED(opened)) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, desktop.Get(),
                                                handler.Get());
        }
        if (SUCCEEDED(closed)) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, desktop.Get(),
                                                handler.Get());
        }
        LSW_AGENT_WARN("uia: AddAutomationEventHandler failed for pid {}: opened {}, closed {}", pid,
                       win::ErrorText::hresult(opened), win::ErrorText::hresult(closed));
        return 0;
    }
    return store({SubscriptionKind::Lifecycle, handler, desktop});
}

std::uint64_t SubscriptionRegistry::subscribe_pid_focus(std::uint32_t pid) {
    if (root_ == nullptr) {
        return 0;
    }
    const auto handler = win::make_com_object<FocusEventHandler>(queue_, static_cast<DWORD>(pid));
    if (const HRESULT hr = root_->AddFocusChangedEventHandler(nullptr, handler.Get()); FAILED(hr)) {
        LSW_AGENT_WARN("uia: AddFocusChangedEventHandler failed for pid {}: {}", pid,
                       win::ErrorText::hresult(hr));
        return 0;
    }
    return store({SubscriptionKind::Focus, handler, nullptr});
}

std::uint64_t SubscriptionRegistry::subscribe_window_properties(std::uintptr_t hwnd) {
    if (root_ == nullptr) {
        return 0;
    }
    const auto element = ElementLocator::from_window(root_, hwnd);
    if (!element) {
        LSW_AGENT_WARN("uia: no element for window {} to watch", hwnd);
        return 0;
    }
    const auto handler = win::make_com_object<PropertyEventHandler>(queue_);
    PROPERTYID properties[] = {UIA_WindowWindowVisualStatePropertyId, UIA_NamePropertyId,
                               UIA_WindowIsModalPropertyId};
    const HRESULT hr = root_->AddPropertyChangedEventHandlerNativeArray(
        element.Get(), TreeScope_Element, nullptr, handler.Get(), properties,
        static_cast<int>(std::size(properties)));
    if (FAILED(hr)) {
        LSW_AGENT_WARN("uia: AddPropertyChangedEventHandler failed for window {}: {}", hwnd,
                       win::ErrorText::hresult(hr));
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
    sub.scope.Reset();
    sub.handler.Reset();
}

}
