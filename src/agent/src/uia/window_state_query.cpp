#include "lsw/agent/uia/window_state_query.hpp"

#include "lsw/agent/uia/element_pattern.hpp"
#include "lsw/agent/uia/uia_window.hpp"

namespace lsw::agent::uia {

WindowState WindowStateQuery::for_window(const UiaWindow& window) {
    WindowState out;
    const auto pattern =
        pattern_as<IUIAutomationWindowPattern>(window.element(), UIA_WindowPatternId);
    if (!pattern) {
        return out;
    }

    out.present = true;
    WindowVisualState visual = WindowVisualState_Normal;
    if (SUCCEEDED(pattern->get_CurrentWindowVisualState(&visual))) {
        out.visual_state = visual_state_name(visual);
    }
    BOOL flag = FALSE;
    if (SUCCEEDED(pattern->get_CurrentIsModal(&flag))) {
        out.is_modal = flag != FALSE;
    }
    if (SUCCEEDED(pattern->get_CurrentIsTopmost(&flag))) {
        out.is_topmost = flag != FALSE;
    }
    return out;
}

const char* WindowStateQuery::visual_state_name(int state) {
    switch (state) {
    case WindowVisualState_Normal:
        return "normal";
    case WindowVisualState_Maximized:
        return "maximized";
    case WindowVisualState_Minimized:
        return "minimized";
    default:
        return "unknown";
    }
}

}  // namespace lsw::agent::uia
