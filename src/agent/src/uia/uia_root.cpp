#include "lsw/agent/uia/uia_root.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
// clang-format on

#include <string>

namespace lsw::agent::uia {

UiaRoot::UiaRoot() {
    IUIAutomation* root = nullptr;
    const HRESULT hr = ::CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                          __uuidof(IUIAutomation), reinterpret_cast<void**>(&root));
    if (FAILED(hr) || root == nullptr) {
        LSW_AGENT_WARN("uia: CoCreateInstance(CUIAutomation) failed hr={}; UIA RPCs will be "
                       "unavailable",
                       win::ErrorText::hresult(hr));
        return;
    }
    root_ = root;
    LSW_AGENT_INFO("uia: IUIAutomation ready");
}

UiaRoot::~UiaRoot() {
    shutdown();
}

void UiaRoot::shutdown() {
    if (root_ != nullptr) {
        root_->Release();
        root_ = nullptr;
    }
}

}  // namespace lsw::agent::uia
