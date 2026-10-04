#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

// Which ``Remove*EventHandler`` overload tears a subscription down.
enum class SubscriptionKind { Lifecycle, Focus, Property };

// What SubscriptionRegistry must remember to detach a COM event handler.
//
// No polymorphic handle: UIA's add/remove calls are not symmetric across
// event kinds, so the kind tag is genuinely load-bearing — it says which
// interface to ask ``handler`` for. ``scope`` is the element the handler
// was attached to — the desktop root for lifecycle, the target window
// for property changes, and nothing at all for focus, which is
// registered process-wide.
struct Subscription {
    SubscriptionKind kind = SubscriptionKind::Lifecycle;
    win::ComPtr<IUnknown> handler;
    win::ComPtr<IUIAutomationElement> scope;
};

}  // namespace lsw::agent::uia
