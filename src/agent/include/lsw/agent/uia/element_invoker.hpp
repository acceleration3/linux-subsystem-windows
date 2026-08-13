#pragma once

#include <cstdint>
#include <string>

struct IUIAutomation;

namespace lsw::agent::uia {

// Programmatic click: finds a control by selector and fires its
// InvokePattern. This is how the host drives a guest control without
// synthesising mouse input at a guessed coordinate.
class ElementInvoker {
public:
    ElementInvoker() = delete;

    // False when the element is not found or exposes no InvokePattern.
    [[nodiscard]] static bool run(IUIAutomation* root, std::uintptr_t hwnd,
                                  const std::string& selector);
};

}  // namespace lsw::agent::uia
