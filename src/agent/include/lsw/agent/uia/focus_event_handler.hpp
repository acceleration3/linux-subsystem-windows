#pragma once

#include "lsw/agent/win/com_object.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

class UiaEventQueue;

// COM handler for focus changes, posting ``uia_focus_changed``.
//
// UIA offers no per-process focus subscription: the handler fires for
// every focus change on the desktop, so the PID filter here is the whole
// subscription. One registration therefore serves any number of host
// mirrors that happen to share a PID.
class FocusEventHandler final : public win::ComObject<IUIAutomationFocusChangedEventHandler> {
public:
    FocusEventHandler(UiaEventQueue& queue, DWORD pid) noexcept : queue_(queue), pid_(pid) {}

    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override;

private:
    UiaEventQueue& queue_;
    DWORD pid_;
};

}  // namespace lsw::agent::uia
