#include "lsw/agent/uia/uia_window.hpp"

#include "lsw/agent/uia/element_locator.hpp"
#include "lsw/agent/util/text_encoding.hpp"

namespace lsw::agent::uia {

UiaWindow::UiaWindow(IUIAutomation* root, std::uintptr_t hwnd)
    : root_(root), element_(ElementLocator::from_window(root, hwnd)) {}

win::ComPtr<IUIAutomationElement> UiaWindow::find(const std::string& selector) const {
    if (element_ == nullptr) {
        return nullptr;
    }
    return ElementLocator::by_selector(root_, element_.Get(),
                                       util::TextEncoding::to_wide(selector));
}

}  // namespace lsw::agent::uia
