#include "lsw/agent/uia/lifecycle_event_handler.hpp"

#include "lsw/agent/uia/bstr_text.hpp"
#include "lsw/agent/uia/uia_event_queue.hpp"

#include <cstdint>

namespace lsw::agent::uia {

HRESULT LifecycleEventHandler::QueryInterface(REFIID iid, void** out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IUIAutomationEventHandler)) {
        *out = static_cast<IUIAutomationEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG LifecycleEventHandler::AddRef() { return ++ref_; }

ULONG LifecycleEventHandler::Release() {
    const ULONG remaining = --ref_;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT LifecycleEventHandler::HandleAutomationEvent(IUIAutomationElement* sender,
                                                     EVENTID event_id) {
    if (sender == nullptr) {
        return S_OK;
    }
    // Registered on the desktop root with subtree scope, so this fires
    // for every process — filter to the one we were asked about.
    int sender_pid = 0;
    sender->get_CurrentProcessId(&sender_pid);
    if (static_cast<DWORD>(sender_pid) != pid_) {
        return S_OK;
    }

    UIA_HWND raw = nullptr;
    sender->get_CurrentNativeWindowHandle(&raw);
    const auto hwnd = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(raw));

    if (event_id == UIA_Window_WindowClosedEventId) {
        queue_.post({"uia_window_closed", {{"hwnd", hwnd}}});
        return S_OK;
    }

    // Opened: carry title + control type so the host can decide which
    // mirror to spawn without a follow-up round trip.
    BSTR name = nullptr;
    sender->get_CurrentName(&name);
    CONTROLTYPEID control_type = 0;
    sender->get_CurrentControlType(&control_type);
    const std::string title = BstrText::to_utf8(name);
    if (name != nullptr) {
        ::SysFreeString(name);
    }
    queue_.post({"uia_window_opened",
                 {{"hwnd", hwnd},
                  {"title", title},
                  {"control_type", static_cast<int>(control_type)},
                  {"pid", static_cast<std::uint64_t>(pid_)}}});
    return S_OK;
}

}  // namespace lsw::agent::uia
