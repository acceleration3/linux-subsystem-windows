#pragma once

#include <cstdint>
#include <string>

struct IUIAutomation;
struct IUIAutomationElement;

namespace lsw::agent::uia {

// Finds UIA elements. Every returned pointer is an owning reference the
// caller must Release.
//
// The selector fallback is the reason this is shared rather than
// re-implemented per query: classic Win32 apps set AutomationId, modern
// Win11 apps frequently leave it empty and identify controls only by
// Name. Matching one and not the other silently finds nothing on half
// the applications in the guest.
class ElementLocator {
public:
    ElementLocator() = delete;

    // Element for a top-level window handle. Null if UIA is unavailable
    // or the handle is not a window.
    [[nodiscard]] static IUIAutomationElement* from_window(IUIAutomation* root,
                                                           std::uintptr_t hwnd);

    // First descendant of ``parent`` whose AutomationId matches
    // ``selector``, else whose Name does. Null when neither matches.
    [[nodiscard]] static IUIAutomationElement* by_selector(IUIAutomation* root,
                                                           IUIAutomationElement* parent,
                                                           const std::wstring& selector);
};

}  // namespace lsw::agent::uia
