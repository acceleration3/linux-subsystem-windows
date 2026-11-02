#pragma once

#include <string>

namespace lsw::agent::uia {

class UiaWindow;

class ElementInvoker {
public:
    ElementInvoker() = delete;

    [[nodiscard]] static bool invoke(const UiaWindow& window,
                                  const std::string& selector);
};

}
