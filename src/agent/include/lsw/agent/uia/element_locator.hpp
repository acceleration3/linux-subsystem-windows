#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

#include <cstdint>
#include <string>

namespace lsw::agent::uia {
class ElementLocator {
public:
    ElementLocator() = delete;

    [[nodiscard]] static win::ComPtr<IUIAutomationElement> from_window(IUIAutomation* root,
                                                                       std::uintptr_t hwnd);

    [[nodiscard]] static win::ComPtr<IUIAutomationElement>
    by_selector(IUIAutomation* root, IUIAutomationElement* parent, const std::wstring& selector);
};

}
