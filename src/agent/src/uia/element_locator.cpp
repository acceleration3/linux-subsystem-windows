#include "lsw/agent/uia/element_locator.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

namespace lsw::agent::uia {

namespace {

// First descendant of ``parent`` whose ``property`` equals ``value``.
[[nodiscard]] IUIAutomationElement* find_by_property(IUIAutomation* root,
                                                     IUIAutomationElement* parent,
                                                     PROPERTYID property,
                                                     const wchar_t* value) {
    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(value);
    IUIAutomationCondition* cond = nullptr;
    IUIAutomationElement* found = nullptr;
    if (SUCCEEDED(root->CreatePropertyCondition(property, v, &cond)) && cond != nullptr) {
        parent->FindFirst(TreeScope_Descendants, cond, &found);
        cond->Release();
    }
    ::VariantClear(&v);
    return found;
}

}  // namespace

IUIAutomationElement* ElementLocator::from_window(IUIAutomation* root,
                                                  std::uintptr_t hwnd) {
    if (root == nullptr || hwnd == 0) {
        return nullptr;
    }
    IUIAutomationElement* element = nullptr;
    if (FAILED(root->ElementFromHandle(reinterpret_cast<HWND>(hwnd), &element))) {
        return nullptr;
    }
    return element;
}

IUIAutomationElement* ElementLocator::by_selector(IUIAutomation* root,
                                                  IUIAutomationElement* parent,
                                                  const std::wstring& selector) {
    if (root == nullptr || parent == nullptr || selector.empty()) {
        return nullptr;
    }
    // AutomationId first — it is the stable one where apps set it at
    // all. Modern Win11 apps often leave it empty and only expose Name.
    if (auto* e = find_by_property(root, parent, UIA_AutomationIdPropertyId,
                                   selector.c_str());
        e != nullptr) {
        return e;
    }
    return find_by_property(root, parent, UIA_NamePropertyId, selector.c_str());
}

}  // namespace lsw::agent::uia
