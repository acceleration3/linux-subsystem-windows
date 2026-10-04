#include "lsw/agent/uia/element_invoker.hpp"

#include "lsw/agent/uia/element_pattern.hpp"
#include "lsw/agent/uia/uia_window.hpp"

namespace lsw::agent::uia {

bool ElementInvoker::invoke(const UiaWindow& window, const std::string& selector) {
    const auto target = window.find(selector);
    const auto pattern = pattern_as<IUIAutomationInvokePattern>(target.Get(), UIA_InvokePatternId);
    if (!pattern) {
        return false;
    }
    return SUCCEEDED(pattern->Invoke());
}

}  // namespace lsw::agent::uia
