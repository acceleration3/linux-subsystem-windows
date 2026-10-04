#pragma once

#include "lsw/agent/win/com_object.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

class UiaEventQueue;

// COM handler for a single window's WindowVisualState / Name / IsModal
// changes.
//
// Emits three distinct topics (``uia_state_changed``,
// ``uia_title_changed``, ``uia_modal_changed``) rather than one generic
// property event, so the host can route each without re-parsing to find
// out what changed.
class PropertyEventHandler final : public win::ComObject<IUIAutomationPropertyChangedEventHandler> {
public:
    explicit PropertyEventHandler(UiaEventQueue& queue) noexcept : queue_(queue) {}

    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement* sender,
                                                         PROPERTYID property,
                                                         VARIANT new_value) override;

private:
    UiaEventQueue& queue_;
};

}  // namespace lsw::agent::uia
