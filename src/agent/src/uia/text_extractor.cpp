#include "lsw/agent/uia/text_extractor.hpp"

#include "lsw/agent/uia/element_pattern.hpp"
#include "lsw/agent/uia/uia_window.hpp"
#include "lsw/agent/win/bstr.hpp"

namespace lsw::agent::uia {

std::string TextExtractor::from_window(const UiaWindow& window, int max_chars) {
    const auto pattern = pattern_as<IUIAutomationTextPattern>(window.element(), UIA_TextPatternId);
    if (!pattern) {
        return {};
    }
    win::ComPtr<IUIAutomationTextRange> range;
    pattern->get_DocumentRange(range.GetAddressOf());
    if (!range) {
        return {};
    }
    win::Bstr text;
    range->GetText(max_chars > 0 ? max_chars : -1, text.put());
    return text.utf8();
}

}  // namespace lsw::agent::uia
