#pragma once

#include "lsw/agent/win/com_object.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

class UiaEventQueue;

class PropertyEventHandler final : public win::ComObject<IUIAutomationPropertyChangedEventHandler> {
public:
    explicit PropertyEventHandler(UiaEventQueue& queue) noexcept : queue_(queue) {}

    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement* sender,
                                                         PROPERTYID property,
                                                         VARIANT new_value) override;

private:
    UiaEventQueue& queue_;
};

}
