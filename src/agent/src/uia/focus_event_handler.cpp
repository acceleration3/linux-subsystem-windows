#include "lsw/agent/uia/focus_event_handler.hpp"

#include "lsw/agent/uia/bstr_text.hpp"
#include "lsw/agent/uia/uia_event_queue.hpp"

#include <cstdint>

namespace lsw::agent::uia {

HRESULT FocusEventHandler::QueryInterface(REFIID iid, void** out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    if (iid == __uuidof(IUnknown)
        || iid == __uuidof(IUIAutomationFocusChangedEventHandler)) {
        *out = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG FocusEventHandler::AddRef() { return ++ref_; }

ULONG FocusEventHandler::Release() {
    const ULONG remaining = --ref_;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT FocusEventHandler::HandleFocusChangedEvent(IUIAutomationElement* sender) {
    if (sender == nullptr) {
        return S_OK;
    }
    // Focus subscriptions are desktop-wide; the PID filter is the whole
    // subscription.
    int sender_pid = 0;
    sender->get_CurrentProcessId(&sender_pid);
    if (static_cast<DWORD>(sender_pid) != pid_) {
        return S_OK;
    }

    UIA_HWND raw = nullptr;
    sender->get_CurrentNativeWindowHandle(&raw);
    BSTR name = nullptr;
    BSTR automation_id = nullptr;
    sender->get_CurrentName(&name);
    sender->get_CurrentAutomationId(&automation_id);

    queue_.post({"uia_focus_changed",
                 {{"hwnd", static_cast<std::uint64_t>(
                               reinterpret_cast<std::uintptr_t>(raw))},
                  {"name", BstrText::to_utf8(name)},
                  {"automation_id", BstrText::to_utf8(automation_id)}}});

    if (name != nullptr) ::SysFreeString(name);
    if (automation_id != nullptr) ::SysFreeString(automation_id);
    return S_OK;
}

}  // namespace lsw::agent::uia
