#include "lsw/agent/uia/caption_button_query.hpp"

#include "lsw/agent/uia/element_locator.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

#include <initializer_list>

namespace lsw::agent::uia {

namespace {

// Screen-space UIA rect -> captured-frame-relative ints. A degenerate
// rect zeroes the slot, which is how "this button is absent" is spelled.
void fill_efb_rect(int& dx, int& dy, int& dw, int& dh, const RECT& screen,
                   int efb_left, int efb_top) {
    const int w = screen.right - screen.left;
    const int h = screen.bottom - screen.top;
    if (w <= 0 || h <= 0) {
        dx = dy = dw = dh = 0;
        return;
    }
    dx = screen.left - efb_left;
    dy = screen.top - efb_top;
    dw = w;
    dh = h;
}

// Fill one slot from the first name that matches. Names are tried in
// order because a button's Name changes with window state.
void fill_first_match(IUIAutomation* root, IUIAutomationElement* scope,
                      std::initializer_list<const wchar_t*> names, int efb_left,
                      int efb_top, int& x, int& y, int& w, int& h) {
    for (const auto* name : names) {
        IUIAutomationElement* button = ElementLocator::by_selector(root, scope, name);
        if (button == nullptr) {
            continue;
        }
        RECT rect{};
        if (SUCCEEDED(button->get_CurrentBoundingRectangle(&rect))) {
            fill_efb_rect(x, y, w, h, rect, efb_left, efb_top);
        }
        button->Release();
        return;
    }
}

// The TitleBar element, when the app has one. Narrowing the search to it
// avoids matching a deeper toolbar button (a tab's close button, say).
[[nodiscard]] IUIAutomationElement* find_title_bar(IUIAutomation* root,
                                                   IUIAutomationElement* window) {
    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = UIA_TitleBarControlTypeId;
    IUIAutomationCondition* cond = nullptr;
    IUIAutomationElement* title_bar = nullptr;
    if (SUCCEEDED(root->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &cond))
        && cond != nullptr) {
        window->FindFirst(TreeScope_Descendants, cond, &title_bar);
        cond->Release();
    }
    ::VariantClear(&v);
    return title_bar;
}

}  // namespace

CaptionButtons CaptionButtonQuery::run(IUIAutomation* root, std::uintptr_t hwnd,
                                       int efb_left, int efb_top) {
    CaptionButtons out;
    IUIAutomationElement* window = ElementLocator::from_window(root, hwnd);
    if (window == nullptr) {
        return out;
    }

    // Edge / Chrome and other fully custom chrome expose no TitleBar at
    // all — their buttons hang off a generic Pane deeper in the tree. In
    // that case search the whole window subtree: FindFirst returns
    // document order, and the title-bar button precedes the in-content
    // "Close tab" / dialog Close.
    IUIAutomationElement* title_bar = find_title_bar(root, window);
    IUIAutomationElement* scope = (title_bar != nullptr) ? title_bar : window;

    fill_first_match(root, scope, {L"Minimize"}, efb_left, efb_top,
                     out.min_x, out.min_y, out.min_w, out.min_h);
    // "Maximize" normally, "Restore"/"Restore Down" once maximised.
    fill_first_match(root, scope, {L"Maximize", L"Restore", L"Restore Down"},
                     efb_left, efb_top, out.max_x, out.max_y, out.max_w, out.max_h);
    fill_first_match(root, scope, {L"Close"}, efb_left, efb_top,
                     out.close_x, out.close_y, out.close_w, out.close_h);
    fill_first_match(root, scope, {L"Help"}, efb_left, efb_top,
                     out.help_x, out.help_y, out.help_w, out.help_h);

    if (title_bar != nullptr) {
        title_bar->Release();
    }
    window->Release();
    return out;
}

}  // namespace lsw::agent::uia
