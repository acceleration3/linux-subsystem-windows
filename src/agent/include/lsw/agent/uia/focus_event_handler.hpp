#pragma once

#include <uiautomation.h>

#include <atomic>

namespace lsw::agent::uia {

class UiaEventQueue;

// COM handler for focus changes, posting ``uia_focus_changed``.
//
// UIA offers no per-process focus subscription: the handler fires for
// every focus change on the desktop, so the PID filter here is the whole
// subscription. One registration therefore serves any number of host
// mirrors that happen to share a PID.
class FocusEventHandler final : public IUIAutomationFocusChangedEventHandler {
public:
    FocusEventHandler(UiaEventQueue& queue, DWORD pid) noexcept
        : queue_(queue), pid_(pid) {}

    HRESULT __stdcall QueryInterface(REFIID iid, void** out) override;
    ULONG __stdcall AddRef() override;
    ULONG __stdcall Release() override;

    HRESULT __stdcall HandleFocusChangedEvent(IUIAutomationElement* sender) override;

private:
    std::atomic<ULONG> ref_{1};
    UiaEventQueue& queue_;
    DWORD pid_;
};

}  // namespace lsw::agent::uia
