#include "lsw/agent/uia/element_invoker.hpp"

#include "lsw/agent/uia/element_locator.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

bool ElementInvoker::run(IUIAutomation* root, std::uintptr_t hwnd,
                         const std::string& selector) {
    IUIAutomationElement* window = ElementLocator::from_window(root, hwnd);
    if (window == nullptr) {
        return false;
    }
    IUIAutomationElement* target =
        ElementLocator::by_selector(root, window, util::TextEncoding::to_wide(selector));
    window->Release();
    if (target == nullptr) {
        return false;
    }
    IUIAutomationInvokePattern* pattern = nullptr;
    target->GetCurrentPatternAs(UIA_InvokePatternId,
                                __uuidof(IUIAutomationInvokePattern),
                                reinterpret_cast<void**>(&pattern));
    target->Release();
    if (pattern == nullptr) {
        return false;
    }
    const HRESULT hr = pattern->Invoke();
    pattern->Release();
    return SUCCEEDED(hr);
}

}  // namespace lsw::agent::uia
