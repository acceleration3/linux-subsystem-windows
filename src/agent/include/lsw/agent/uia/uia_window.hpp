#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

#include <cstdint>
#include <string>

namespace lsw::agent::uia {
class UiaWindow {
public:
    UiaWindow(IUIAutomation* root, std::uintptr_t hwnd);

    UiaWindow(const UiaWindow&) = delete;
    UiaWindow& operator=(const UiaWindow&) = delete;
    UiaWindow(UiaWindow&&) = delete;
    UiaWindow& operator=(UiaWindow&&) = delete;

    [[nodiscard]] bool valid() const noexcept { return element_ != nullptr; }

    [[nodiscard]] IUIAutomation* automation() const noexcept { return root_; }

    [[nodiscard]] IUIAutomationElement* element() const noexcept { return element_.Get(); }

    [[nodiscard]] win::ComPtr<IUIAutomationElement> find(const std::string& selector) const;

private:
    IUIAutomation* root_;
    win::ComPtr<IUIAutomationElement> element_;
};

}
