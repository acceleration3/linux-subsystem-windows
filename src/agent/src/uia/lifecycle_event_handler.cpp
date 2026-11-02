#include "lsw/agent/uia/lifecycle_event_handler.hpp"

#include "lsw/agent/uia/uia_event_queue.hpp"
#include "lsw/agent/win/bstr.hpp"

#include <cstdint>

namespace lsw::agent::uia {

HRESULT LifecycleEventHandler::HandleAutomationEvent(IUIAutomationElement* sender,
                                                     EVENTID event_id) {
    if (sender == nullptr) {
        return S_OK;
    }
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

    win::Bstr name;
    sender->get_CurrentName(name.put());
    CONTROLTYPEID control_type = 0;
    sender->get_CurrentControlType(&control_type);
    queue_.post({"uia_window_opened",
                 {{"hwnd", hwnd},
                  {"title", name.utf8()},
                  {"control_type", static_cast<int>(control_type)},
                  {"pid", static_cast<std::uint64_t>(pid_)}}});
    return S_OK;
}

}
