#include "lsw/agent/uia/element_locator.hpp"

namespace lsw::agent::uia {

namespace {

// First descendant of ``parent`` whose ``property`` equals ``value``.
[[nodiscard]] win::ComPtr<IUIAutomationElement> find_by_property(IUIAutomation* root,
                                                                 IUIAutomationElement* parent,
                                                                 PROPERTYID property,
                                                                 const wchar_t* value) {
    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(value);
    win::ComPtr<IUIAutomationCondition> cond;
    win::ComPtr<IUIAutomationElement> found;
    if (SUCCEEDED(root->CreatePropertyCondition(property, v, cond.GetAddressOf())) && cond) {
        parent->FindFirst(TreeScope_Descendants, cond.Get(), found.GetAddressOf());
    }
    ::VariantClear(&v);
    return found;
}

}  // namespace

win::ComPtr<IUIAutomationElement> ElementLocator::from_window(IUIAutomation* root,
                                                              std::uintptr_t hwnd) {
    win::ComPtr<IUIAutomationElement> element;
    if (root == nullptr || hwnd == 0) {
        return element;
    }
    if (FAILED(root->ElementFromHandle(reinterpret_cast<HWND>(hwnd), element.GetAddressOf()))) {
        element.Reset();
    }
    return element;
}

win::ComPtr<IUIAutomationElement> ElementLocator::by_selector(IUIAutomation* root,
                                                              IUIAutomationElement* parent,
                                                              const std::wstring& selector) {
    if (root == nullptr || parent == nullptr || selector.empty()) {
        return nullptr;
    }
    // AutomationId first — it is the stable one where apps set it at
    // all. Modern Win11 apps often leave it empty and only expose Name.
    if (auto e = find_by_property(root, parent, UIA_AutomationIdPropertyId, selector.c_str())) {
        return e;
    }
    return find_by_property(root, parent, UIA_NamePropertyId, selector.c_str());
}

}  // namespace lsw::agent::uia
