#pragma once

#include <cstdint>
#include <string>

struct IUIAutomation;
struct IUIAutomationElement;

namespace lsw::agent::uia {

// A guest window as the UIA element tree sees it, resolved once.
//
// Every query in this directory needs the same two things — the
// automation root and the window handle — and each one used to take
// both, call ElementLocator::from_window itself, and Release the result
// on each of its exit paths. That is the pair being threaded through
// five signatures and the same four lines repeated five times, once per
// operation, with a manual Release that any new early return would have
// to remember.
//
// So the pair is the object. It holds the root, resolves the element in
// its constructor and releases it in its destructor, and the operations
// take one argument that cannot be half-initialised.
//
// Cheap enough to build per call, which is what the callers do: one
// FindFirst against the window handle, the same cost the five queries
// were each paying anyway.
class UiaWindow {
public:
    UiaWindow(IUIAutomation* root, std::uintptr_t hwnd) noexcept;
    ~UiaWindow();

    UiaWindow(const UiaWindow&) = delete;
    UiaWindow& operator=(const UiaWindow&) = delete;
    UiaWindow(UiaWindow&&) = delete;
    UiaWindow& operator=(UiaWindow&&) = delete;

    // False when UIA is unavailable or the handle names no window.
    // Every operation checks this rather than assuming.
    [[nodiscard]] bool valid() const noexcept { return element_ != nullptr; }

    [[nodiscard]] IUIAutomation* automation() const noexcept { return root_; }
    [[nodiscard]] IUIAutomationElement* element() const noexcept { return element_; }

    // First descendant whose AutomationId matches ``selector``, else
    // whose Name does. Null when neither matches, and when it is not
    // null it is an owning reference the caller must Release.
    [[nodiscard]] IUIAutomationElement* find(const std::string& selector) const;

private:
    IUIAutomation* root_;                    // borrowed from UiaRoot
    IUIAutomationElement* element_ = nullptr;  // COM ref-counted, ours
};

}  // namespace lsw::agent::uia
