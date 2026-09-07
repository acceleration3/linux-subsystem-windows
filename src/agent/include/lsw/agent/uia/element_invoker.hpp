#pragma once

#include <string>

namespace lsw::agent::uia {

class UiaWindow;

// Programmatic click: finds a control by selector and fires its
// InvokePattern. This is how the host drives a guest control without
// synthesising mouse input at a guessed coordinate.
class ElementInvoker {
public:
    ElementInvoker() = delete;

    // False when the element is not found or exposes no InvokePattern.
    [[nodiscard]] static bool invoke(const UiaWindow& window,
                                  const std::string& selector);
};

}  // namespace lsw::agent::uia
