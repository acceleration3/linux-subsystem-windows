#pragma once

#include "lsw/agent/win/com_object.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

class UiaEventQueue;

// COM handler for window opened/closed, posting ``uia_window_opened`` /
// ``uia_window_closed``.
//
// Registered against the desktop root with subtree scope so it catches
// dialogs and popups the app spawns later — which means it also hears
// about every other process, so it filters by ProcessId itself. That
// filter is why the PID is a member rather than a subscription detail.
//
// Lives on UIA's thread pool; posts into the queue and returns.
class LifecycleEventHandler final : public win::ComObject<IUIAutomationEventHandler> {
public:
    LifecycleEventHandler(UiaEventQueue& queue, DWORD pid) noexcept : queue_(queue), pid_(pid) {}

    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement* sender,
                                                    EVENTID event_id) override;

private:
    UiaEventQueue& queue_;
    DWORD pid_;
};

}  // namespace lsw::agent::uia
