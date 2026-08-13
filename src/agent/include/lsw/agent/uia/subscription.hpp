#pragma once

struct IUIAutomationElement;

namespace lsw::agent::uia {

// Which ``Remove*EventHandler`` overload tears a subscription down.
enum class SubscriptionKind { Lifecycle, Focus, Property };

// What SubscriptionRegistry must remember to detach a COM event handler.
//
// No polymorphic handle: UIA's add/remove calls are not symmetric across
// event kinds, so the kind tag is genuinely load-bearing. ``scope`` is
// the element the handler was attached to — the desktop root for
// lifecycle, the target window for property changes, and nothing at all
// for focus, which is registered process-wide.
struct Subscription {
    SubscriptionKind kind = SubscriptionKind::Lifecycle;
    void* handler = nullptr;                // IUIAutomation*EventHandler*, COM-owned
    IUIAutomationElement* scope = nullptr;  // element the handler is bound to
};

}  // namespace lsw::agent::uia
