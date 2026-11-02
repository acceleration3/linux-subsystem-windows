#pragma once

#include "lsw/agent/win/com_object.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

class UiaEventQueue;

class LifecycleEventHandler final : public win::ComObject<IUIAutomationEventHandler> {
public:
    LifecycleEventHandler(UiaEventQueue& queue, DWORD pid) noexcept : queue_(queue), pid_(pid) {}

    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement* sender,
                                                    EVENTID event_id) override;

private:
    UiaEventQueue& queue_;
    DWORD pid_;
};

}
