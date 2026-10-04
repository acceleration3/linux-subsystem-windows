#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

// ``element``'s control pattern ``id`` as interface ``Pattern``, or null
// when the element is null or does not support that pattern.
//
// GetCurrentPatternAs takes the IID and the out-pointer separately, so
// every call site restated the interface twice and cast through void**;
// this states it once, in the template argument.
template<class Pattern>
[[nodiscard]] win::ComPtr<Pattern> pattern_as(IUIAutomationElement* element, PATTERNID id) {
    win::ComPtr<Pattern> pattern;
    if (element != nullptr) {
        element->GetCurrentPatternAs(id, __uuidof(Pattern),
                                     reinterpret_cast<void**>(pattern.ReleaseAndGetAddressOf()));
    }
    return pattern;
}

}  // namespace lsw::agent::uia
