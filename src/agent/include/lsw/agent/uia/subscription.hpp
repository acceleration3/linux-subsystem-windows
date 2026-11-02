#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {
enum class SubscriptionKind { Lifecycle, Focus, Property };

struct Subscription {
    SubscriptionKind kind = SubscriptionKind::Lifecycle;
    win::ComPtr<IUnknown> handler;
    win::ComPtr<IUIAutomationElement> scope;
};

}
