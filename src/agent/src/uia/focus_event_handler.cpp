#include "lsw/agent/uia/focus_event_handler.hpp"

#include "lsw/agent/uia/uia_event_queue.hpp"
#include "lsw/agent/win/bstr.hpp"

#include <cstdint>

namespace lsw::agent::uia {

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
    win::Bstr name;
    win::Bstr automation_id;
    sender->get_CurrentName(name.put());
    sender->get_CurrentAutomationId(automation_id.put());

    queue_.post({"uia_focus_changed",
                 {{"hwnd", static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(raw))},
                  {"name", name.utf8()},
                  {"automation_id", automation_id.utf8()}}});
    return S_OK;
}

}  // namespace lsw::agent::uia
