#include "lsw/agent/uia/element_invoker.hpp"

#include "lsw/agent/uia/uia_window.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

bool ElementInvoker::invoke(const UiaWindow& window, const std::string& selector) {
    IUIAutomationElement* target = window.find(selector);
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
