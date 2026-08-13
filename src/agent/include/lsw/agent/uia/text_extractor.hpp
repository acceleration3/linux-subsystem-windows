#pragma once

#include <cstdint>
#include <string>

struct IUIAutomation;

namespace lsw::agent::uia {

// Reads all text under a window through its TextPattern — how the host
// gets a document's contents without screen-scraping pixels.
class TextExtractor {
public:
    TextExtractor() = delete;

    // ``max_chars`` caps the result so a large document cannot produce
    // an unbounded wire payload; <= 0 means no cap. Empty for windows
    // with no TextPattern.
    [[nodiscard]] static std::string run(IUIAutomation* root, std::uintptr_t hwnd,
                                         int max_chars);
};

}  // namespace lsw::agent::uia
