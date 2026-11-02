#pragma once

#include <string>

namespace lsw::agent::uia {

class UiaWindow;

class ElementValueSetter {
public:
    ElementValueSetter() = delete;

    [[nodiscard]] static bool set_value(const UiaWindow& window,
                                  const std::string& selector,
                                  const std::string& value);
};

}
