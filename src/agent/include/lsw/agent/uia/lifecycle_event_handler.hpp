#pragma once

#include <uiautomation.h>

#include <atomic>

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
class LifecycleEventHandler final : public IUIAutomationEventHandler {
public:
    LifecycleEventHandler(UiaEventQueue& queue, DWORD pid) noexcept
        : queue_(queue), pid_(pid) {}

    HRESULT __stdcall QueryInterface(REFIID iid, void** out) override;
    ULONG __stdcall AddRef() override;
    ULONG __stdcall Release() override;

    HRESULT __stdcall HandleAutomationEvent(IUIAutomationElement* sender,
                                            EVENTID event_id) override;

private:
    std::atomic<ULONG> ref_{1};
    UiaEventQueue& queue_;
    DWORD pid_;
};

}  // namespace lsw::agent::uia
