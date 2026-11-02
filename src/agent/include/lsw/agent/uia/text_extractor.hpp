#pragma once

#include <string>

namespace lsw::agent::uia {

class UiaWindow;

class TextExtractor {
public:
    TextExtractor() = delete;

    [[nodiscard]] static std::string from_window(const UiaWindow& window,
                                         int max_chars);
};

}
