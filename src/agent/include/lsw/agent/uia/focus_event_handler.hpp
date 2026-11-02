#pragma once

#include "lsw/agent/win/com_object.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

class UiaEventQueue;

class FocusEventHandler final : public win::ComObject<IUIAutomationFocusChangedEventHandler> {
public:
    FocusEventHandler(UiaEventQueue& queue, DWORD pid) noexcept : queue_(queue), pid_(pid) {}

    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override;

private:
    UiaEventQueue& queue_;
    DWORD pid_;
};

}
