#pragma once

#include "lsw/agent/win/com_ptr.hpp"

// clang-format off
#include <windows.h>
#include <uiautomation.h>
// clang-format on

#include <cstdint>
#include <string>

namespace lsw::agent::uia {

// A guest window as the UIA element tree sees it, resolved once.
//
// Every query in this directory needs the same two things — the
// automation root and the window handle — and each one used to take
// both, call ElementLocator::from_window itself, and Release the result
// on each of its exit paths.
//
// So the pair is the object. It holds the root and resolves the element
// in its constructor, and the operations take one argument that cannot
// be half-initialised.
//
// Cheap enough to build per call, which is what the callers do: one
// ElementFromHandle against the window handle, the same cost the queries
// were each paying anyway.
class UiaWindow {
public:
    UiaWindow(IUIAutomation* root, std::uintptr_t hwnd);

    UiaWindow(const UiaWindow&) = delete;
    UiaWindow& operator=(const UiaWindow&) = delete;
    UiaWindow(UiaWindow&&) = delete;
    UiaWindow& operator=(UiaWindow&&) = delete;

    // False when UIA is unavailable or the handle names no window.
    // Every operation checks this rather than assuming.
    [[nodiscard]] bool valid() const noexcept { return element_ != nullptr; }

    [[nodiscard]] IUIAutomation* automation() const noexcept { return root_; }

    [[nodiscard]] IUIAutomationElement* element() const noexcept { return element_.Get(); }

    // First descendant whose AutomationId matches ``selector``, else
    // whose Name does. Null when neither matches.
    [[nodiscard]] win::ComPtr<IUIAutomationElement> find(const std::string& selector) const;

private:
    IUIAutomation* root_;  // borrowed from UiaRoot
    win::ComPtr<IUIAutomationElement> element_;
};

}  // namespace lsw::agent::uia
