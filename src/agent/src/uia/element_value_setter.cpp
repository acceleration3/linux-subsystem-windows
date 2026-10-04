#include "lsw/agent/uia/element_value_setter.hpp"

#include "lsw/agent/uia/element_pattern.hpp"
#include "lsw/agent/uia/uia_window.hpp"
#include "lsw/agent/win/bstr.hpp"

namespace lsw::agent::uia {

bool ElementValueSetter::set_value(const UiaWindow& window, const std::string& selector,
                                   const std::string& value) {
    const auto target = window.find(selector);
    const auto pattern = pattern_as<IUIAutomationValuePattern>(target.Get(), UIA_ValuePatternId);
    if (!pattern) {
        return false;
    }
    const win::Bstr text = win::Bstr::from_utf8(value);
    return SUCCEEDED(pattern->SetValue(text.get()));
}

}  // namespace lsw::agent::uia
