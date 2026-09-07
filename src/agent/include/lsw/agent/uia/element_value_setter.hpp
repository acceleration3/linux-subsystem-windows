#pragma once

#include <cstdint>
#include <string>

struct IUIAutomation;

namespace lsw::agent::uia {

// Sets a control's text through its ValuePattern — filling a field
// without typing into it key by key.
class ElementValueSetter {
public:
    ElementValueSetter() = delete;

    // False when the element is not found or exposes no ValuePattern.
    [[nodiscard]] static bool set_value(IUIAutomation* root, std::uintptr_t hwnd,
                                  const std::string& selector,
                                  const std::string& value);
};

}  // namespace lsw::agent::uia
