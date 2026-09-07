#include "lsw/agent/uia/uia_window.hpp"

#include "lsw/agent/uia/element_locator.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

UiaWindow::UiaWindow(IUIAutomation* root, std::uintptr_t hwnd) noexcept
    : root_(root), element_(ElementLocator::from_window(root, hwnd)) {}

UiaWindow::~UiaWindow() {
    if (element_ != nullptr) {
        element_->Release();
    }
}

IUIAutomationElement* UiaWindow::find(const std::string& selector) const {
    if (element_ == nullptr) {
        return nullptr;
    }
    return ElementLocator::by_selector(root_, element_,
                                       util::TextEncoding::to_wide(selector));
}

}  // namespace lsw::agent::uia
