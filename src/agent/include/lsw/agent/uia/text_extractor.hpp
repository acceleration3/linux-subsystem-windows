#pragma once

#include <string>

namespace lsw::agent::uia {

class UiaWindow;

// Reads all text under a window through its TextPattern — how the host
// gets a document's contents without screen-scraping pixels.
class TextExtractor {
public:
    TextExtractor() = delete;

    // ``max_chars`` caps the result so a large document cannot produce
    // an unbounded wire payload; <= 0 means no cap. Empty for windows
    // with no TextPattern.
    [[nodiscard]] static std::string from_window(const UiaWindow& window,
                                         int max_chars);
};

}  // namespace lsw::agent::uia
