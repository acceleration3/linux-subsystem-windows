#pragma once

#include <uiautomation.h>

#include <atomic>

namespace lsw::agent::uia {

class UiaEventQueue;

// COM handler for a single window's WindowVisualState / Name / IsModal
// changes.
//
// Emits three distinct topics (``uia_state_changed``,
// ``uia_title_changed``, ``uia_modal_changed``) rather than one generic
// property event, so the host can route each without re-parsing to find
// out what changed.
class PropertyEventHandler final : public IUIAutomationPropertyChangedEventHandler {
public:
    explicit PropertyEventHandler(UiaEventQueue& queue) noexcept : queue_(queue) {}

    HRESULT __stdcall QueryInterface(REFIID iid, void** out) override;
    ULONG __stdcall AddRef() override;
    ULONG __stdcall Release() override;

    HRESULT __stdcall HandlePropertyChangedEvent(IUIAutomationElement* sender,
                                                 PROPERTYID property,
                                                 VARIANT new_value) override;

private:
    std::atomic<ULONG> ref_{1};
    UiaEventQueue& queue_;
};

}  // namespace lsw::agent::uia
