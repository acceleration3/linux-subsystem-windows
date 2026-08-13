#include "lsw/agent/uia/window_state_query.hpp"

#include "lsw/agent/uia/element_locator.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

WindowState WindowStateQuery::run(IUIAutomation* root, std::uintptr_t hwnd) {
    WindowState out;
    IUIAutomationElement* element = ElementLocator::from_window(root, hwnd);
    if (element == nullptr) {
        return out;
    }
    IUIAutomationWindowPattern* pattern = nullptr;
    element->GetCurrentPatternAs(UIA_WindowPatternId,
                                 __uuidof(IUIAutomationWindowPattern),
                                 reinterpret_cast<void**>(&pattern));
    element->Release();
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
