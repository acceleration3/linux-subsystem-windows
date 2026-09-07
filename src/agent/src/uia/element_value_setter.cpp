#include "lsw/agent/uia/element_value_setter.hpp"

#include "lsw/agent/uia/uia_window.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

bool ElementValueSetter::set_value(const UiaWindow& window, const std::string& selector,
                                   const std::string& value) {
    IUIAutomationElement* target = window.find(selector);
    if (target == nullptr) {
        return false;
    }
    IUIAutomationValuePattern* pattern = nullptr;
    target->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern),
                                reinterpret_cast<void**>(&pattern));
    target->Release();
    if (pattern == nullptr) {
        return false;
    }
    BSTR text = ::SysAllocString(util::TextEncoding::to_wide(value).c_str());
    const HRESULT hr = pattern->SetValue(text);
    ::SysFreeString(text);
    pattern->Release();
    return SUCCEEDED(hr);
}

}  // namespace lsw::agent::uia
