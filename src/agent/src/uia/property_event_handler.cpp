#include "lsw/agent/uia/property_event_handler.hpp"

#include "lsw/agent/uia/bstr_text.hpp"
#include "lsw/agent/uia/uia_event_queue.hpp"

#include <cstdint>
#include <string>

namespace lsw::agent::uia {

namespace {

[[nodiscard]] const char* visual_state_name(const VARIANT& value) {
    if (value.vt != VT_I4) {
        return "unknown";
    }
    switch (value.lVal) {
    case WindowVisualState_Normal:    return "normal";
    case WindowVisualState_Maximized: return "maximized";
    case WindowVisualState_Minimized: return "minimized";
    default:                          return "unknown";
    }
}

}  // namespace

HRESULT PropertyEventHandler::QueryInterface(REFIID iid, void** out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    if (iid == __uuidof(IUnknown)
        || iid == __uuidof(IUIAutomationPropertyChangedEventHandler)) {
        *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG PropertyEventHandler::AddRef() { return ++ref_; }

ULONG PropertyEventHandler::Release() {
    const ULONG remaining = --ref_;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT PropertyEventHandler::HandlePropertyChangedEvent(IUIAutomationElement* sender,
                                                         PROPERTYID property,
                                                         VARIANT new_value) {
    if (sender == nullptr) {
        return S_OK;
    }
    UIA_HWND raw = nullptr;
    sender->get_CurrentNativeWindowHandle(&raw);
    const auto hwnd = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(raw));

    if (property == UIA_WindowWindowVisualStatePropertyId) {
        queue_.post({"uia_state_changed",
                     {{"hwnd", hwnd}, {"state", std::string{visual_state_name(new_value)}}}});
    } else if (property == UIA_NamePropertyId && new_value.vt == VT_BSTR) {
        queue_.post({"uia_title_changed",
                     {{"hwnd", hwnd}, {"title", BstrText::to_utf8(new_value.bstrVal)}}});
    } else if (property == UIA_WindowIsModalPropertyId && new_value.vt == VT_BOOL) {
        queue_.post({"uia_modal_changed",
                     {{"hwnd", hwnd}, {"is_modal", new_value.boolVal != VARIANT_FALSE}}});
    }
    return S_OK;
}

}  // namespace lsw::agent::uia
