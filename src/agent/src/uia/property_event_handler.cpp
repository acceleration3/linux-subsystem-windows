#include "lsw/agent/uia/property_event_handler.hpp"

#include "lsw/agent/uia/uia_event_queue.hpp"
#include "lsw/agent/uia/window_state_query.hpp"
#include "lsw/agent/win/bstr.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::uia {

HRESULT PropertyEventHandler::HandlePropertyChangedEvent(IUIAutomationElement* sender,
                                                         PROPERTYID property, VARIANT new_value) {
    if (sender == nullptr) {
        return S_OK;
    }
    UIA_HWND raw = nullptr;
    sender->get_CurrentNativeWindowHandle(&raw);
    const auto hwnd = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(raw));

    if (property == UIA_WindowWindowVisualStatePropertyId) {
        // -1 is no WindowVisualState, so a value of the wrong type reads
        // as "unknown" rather than as whatever its bits happen to be.
        const int state = new_value.vt == VT_I4 ? static_cast<int>(new_value.lVal) : -1;
        queue_.post(
            {"uia_state_changed",
             {{"hwnd", hwnd}, {"state", std::string{WindowStateQuery::visual_state_name(state)}}}});
    } else if (property == UIA_NamePropertyId && new_value.vt == VT_BSTR) {
        queue_.post({"uia_title_changed",
                     {{"hwnd", hwnd}, {"title", win::Bstr::to_utf8(new_value.bstrVal)}}});
    } else if (property == UIA_WindowIsModalPropertyId && new_value.vt == VT_BOOL) {
        queue_.post({"uia_modal_changed",
                     {{"hwnd", hwnd}, {"is_modal", new_value.boolVal != VARIANT_FALSE}}});
    }
    return S_OK;
}

}  // namespace lsw::agent::uia
