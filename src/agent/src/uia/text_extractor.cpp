#include "lsw/agent/uia/text_extractor.hpp"

#include "lsw/agent/uia/bstr_text.hpp"
#include "lsw/agent/uia/uia_window.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

std::string TextExtractor::from_window(const UiaWindow& window, int max_chars) {
    IUIAutomationElement* element = window.element();
    if (element == nullptr) {
        return {};
    }
    IUIAutomationTextPattern* pattern = nullptr;
    element->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern),
                                 reinterpret_cast<void**>(&pattern));
    if (pattern == nullptr) {
        return {};
    }
    IUIAutomationTextRange* range = nullptr;
    pattern->get_DocumentRange(&range);
    pattern->Release();
    if (range == nullptr) {
        return {};
    }
    BSTR text = nullptr;
    range->GetText(max_chars > 0 ? max_chars : -1, &text);
    range->Release();
    std::string out = BstrText::to_utf8(text);
    if (text != nullptr) {
        ::SysFreeString(text);
    }
    return out;
}

}  // namespace lsw::agent::uia
