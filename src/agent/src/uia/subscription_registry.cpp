#include "lsw/agent/uia/subscription_registry.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/uia/element_locator.hpp"
#include "lsw/agent/uia/focus_event_handler.hpp"
#include "lsw/agent/uia/lifecycle_event_handler.hpp"
#include "lsw/agent/uia/property_event_handler.hpp"
#include "lsw/agent/uia/uia_event_queue.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

std::uint64_t SubscriptionRegistry::store(Subscription sub) {
    const std::lock_guard<std::mutex> lock(mu_);
    const std::uint64_t id = next_id_++;
    subs_.emplace(id, sub);
    return id;
}

std::uint64_t SubscriptionRegistry::subscribe_pid_lifecycle(std::uint32_t pid) {
    if (root_ == nullptr) {
        return 0;
    }
    IUIAutomationElement* desktop = nullptr;
    if (FAILED(root_->GetRootElement(&desktop)) || desktop == nullptr) {
        return 0;
    }
    auto* handler = new LifecycleEventHandler(queue_, static_cast<DWORD>(pid));

    // Subtree scope catches the app's root window AND any dialogs or
    // popups it spawns later; the handler filters by PID.
    const HRESULT opened = root_->AddAutomationEventHandler(
        UIA_Window_WindowOpenedEventId, desktop, TreeScope_Subtree, nullptr, handler);
    const HRESULT closed = root_->AddAutomationEventHandler(
        UIA_Window_WindowClosedEventId, desktop, TreeScope_Subtree, nullptr, handler);
    if (FAILED(opened) || FAILED(closed)) {
        // Roll back whichever half succeeded so we do not leave a live
        // handler with no id to detach it by.
        if (SUCCEEDED(opened)) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, desktop,
                                                handler);
        }
        if (SUCCEEDED(closed)) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, desktop,
                                                handler);
        }
        handler->Release();
        desktop->Release();
        LSW_AGENT_WARN("uia: AddAutomationEventHandler failed");
        return 0;
    }
    return store({SubscriptionKind::Lifecycle, handler, desktop});
}

std::uint64_t SubscriptionRegistry::subscribe_pid_focus(std::uint32_t pid) {
    if (root_ == nullptr) {
        return 0;
    }
    auto* handler = new FocusEventHandler(queue_, static_cast<DWORD>(pid));
    if (FAILED(root_->AddFocusChangedEventHandler(nullptr, handler))) {
        handler->Release();
        return 0;
    }
    // Focus handlers are process-wide, so there is no scope element.
    return store({SubscriptionKind::Focus, handler, nullptr});
}

std::uint64_t SubscriptionRegistry::subscribe_window_properties(std::uintptr_t hwnd) {
    if (root_ == nullptr) {
        return 0;
    }
    IUIAutomationElement* element = ElementLocator::from_window(root_, hwnd);
    if (element == nullptr) {
        return 0;
    }
    auto* handler = new PropertyEventHandler(queue_);
    const PROPERTYID properties[] = {UIA_WindowWindowVisualStatePropertyId,
                                     UIA_NamePropertyId, UIA_WindowIsModalPropertyId};
    const HRESULT hr = root_->AddPropertyChangedEventHandlerNativeArray(
        element, TreeScope_Element, nullptr, handler,
        const_cast<PROPERTYID*>(properties),
        static_cast<int>(sizeof(properties) / sizeof(properties[0])));
    if (FAILED(hr)) {
        handler->Release();
        element->Release();
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
        sub = it->second;
        subs_.erase(it);
    }
    if (sub.handler == nullptr) {
        return;
    }
    // Each kind detaches differently — this asymmetry is why Subscription
    // carries a kind tag rather than a single polymorphic handle.
    switch (sub.kind) {
    case SubscriptionKind::Lifecycle: {
        auto* handler = static_cast<IUIAutomationEventHandler*>(sub.handler);
        if (sub.scope != nullptr) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId, sub.scope,
                                                handler);
            root_->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId, sub.scope,
                                                handler);
            sub.scope->Release();
        }
        handler->Release();
        break;
    }
    case SubscriptionKind::Focus: {
        auto* handler = static_cast<IUIAutomationFocusChangedEventHandler*>(sub.handler);
        root_->RemoveFocusChangedEventHandler(handler);
        handler->Release();
        break;
    }
    case SubscriptionKind::Property: {
        auto* handler =
            static_cast<IUIAutomationPropertyChangedEventHandler*>(sub.handler);
        if (sub.scope != nullptr) {
            root_->RemovePropertyChangedEventHandler(sub.scope, handler);
            sub.scope->Release();
        }
        handler->Release();
        break;
    }
    }
}

}  // namespace lsw::agent::uia
