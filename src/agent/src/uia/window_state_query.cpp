#include "lsw/agent/uia/window_state_query.hpp"

#include "lsw/agent/uia/uia_window.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

WindowState WindowStateQuery::for_window(const UiaWindow& window) {
    WindowState out;
    IUIAutomationElement* element = window.element();
    if (element == nullptr) {
        return out;
    }
    IUIAutomationWindowPattern* pattern = nullptr;
    element->GetCurrentPatternAs(UIA_WindowPatternId,
                                 __uuidof(IUIAutomationWindowPattern),
                                 reinterpret_cast<void**>(&pattern));
    if (pattern == nullptr) {
        return out;
    }

    out.present = true;
    WindowVisualState visual = WindowVisualState_Normal;
    if (SUCCEEDED(pattern->get_CurrentWindowVisualState(&visual))) {
        switch (visual) {
        case WindowVisualState_Normal:    out.visual_state = "normal";    break;
        case WindowVisualState_Maximized: out.visual_state = "maximized"; break;
        case WindowVisualState_Minimized: out.visual_state = "minimized"; break;
        }
    }
    BOOL flag = FALSE;
    if (SUCCEEDED(pattern->get_CurrentIsModal(&flag))) {
        out.is_modal = flag != FALSE;
    }
    if (SUCCEEDED(pattern->get_CurrentIsTopmost(&flag))) {
        out.is_topmost = flag != FALSE;
    }
    pattern->Release();
    return out;
}

}  // namespace lsw::agent::uia
