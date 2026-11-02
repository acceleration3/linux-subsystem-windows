#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {
template<class Pattern>
[[nodiscard]] win::ComPtr<Pattern> pattern_as(IUIAutomationElement* element, PATTERNID id) {
    win::ComPtr<Pattern> pattern;
    if (element != nullptr) {
        element->GetCurrentPatternAs(id, __uuidof(Pattern),
                                     reinterpret_cast<void**>(pattern.ReleaseAndGetAddressOf()));
    }
    return pattern;
}

}
