#pragma once

#include "lsw/agent/rpc/event.hpp"
#include "lsw/agent/rpc/event_source.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward-decl the COM interfaces so this header doesn't drag the
// whole UIAutomation.h in. The .cpp resolves them. The public API
// uses ``std::uintptr_t`` for HWND values (matching ``win::Window``)
// so this header has no Win32 dependency.
struct IUIAutomation;
struct IUIAutomationEventHandler;
struct IUIAutomationElement;

namespace lsw::agent::uia {

// Caption-button rects as reported by UIA. All values are EFB-
// relative pixels (origin at the captured frame's top-left). Zero
// w/h on a slot means "this button isn't present on the window".
//
// Used by the host's mouse-press handler for a LOCAL O(1) check —
// no RPC. Caption-drag detection (everything that ISN'T a button)
// still uses ``WM_NCHITTEST`` per-press; modern apps put tabs /
// address bars / toolbars on the caption row and UIA can't
// discriminate those from the empty caption strip on its own, but
// the OS frame's hit-test (DefWindowProc's WM_NCHITTEST handler)
// correctly returns HTCAPTION only for the dead caption strip.
struct CaptionButtons {
    int min_x = 0,   min_y = 0,   min_w = 0,   min_h = 0;
    int max_x = 0,   max_y = 0,   max_w = 0,   max_h = 0;
    int close_x = 0, close_y = 0, close_w = 0, close_h = 0;
    int help_x = 0,  help_y = 0,  help_w = 0,  help_h = 0;
};

// Microsoft UI Automation client wrapper. Owns the
// ``IUIAutomation`` root object for the lifetime of the agent
// process; provides synchronous queries (Phase 7+: title-bar layout,
// window state, programmatic invoke / text read / tree dump) and
// per-session event subscriptions (Phase 9: window opened/closed,
// state changed, title changed, focus changed) that push into the
// shared queue drained on the transport's 33 ms tick.
//
// Phase 6 skeleton: COM init + IUIAutomation creation + clean
// release. ``collect_pending`` returns nothing yet.
//
// Threading:
//   - Constructed on the agent's main thread after the COM apartment
//     guard. Holds a single ``IUIAutomation*`` retrieved via
//     ``CoCreateInstance(CLSID_CUIAutomation, ...)``.
//   - Synchronous query methods (Phase 7+) are called from the
//     dispatcher thread; UIA marshals across the COM boundary.
//   - Event handler callbacks (Phase 9) fire on UIA's own thread pool
//     — they grab ``mu_``, append to ``pending_``, return immediately.
//     ``collect_pending`` drains under the same lock.
class Uia : public rpc::EventSource {
public:
    Uia();
    ~Uia() override;
    Uia(const Uia&) = delete;
    Uia& operator=(const Uia&) = delete;

    // Phase 6: release the ``IUIAutomation`` pointer + any future
    // event-handler refs. Must be called by ``AgentServices::shutdown``
    // BEFORE the process-lifetime ``win::Apartment`` destructor runs.
    void shutdown();

    // True once the IUIAutomation root has been created. False on
    // hosts where UIA is unavailable (extremely rare on Win10+) — in
    // that case every Uia method short-circuits to a benign no-op so
    // handlers stay correct.
    [[nodiscard]] bool ok() const noexcept { return root_ != nullptr; }

    // Caption-button rects for ``hwnd`` via UIA. ``efb_left`` /
    // ``efb_top`` are the screen coords of the captured frame's
    // origin (top-left of the DWM extended frame); returned rects
    // are translated into EFB-relative space so they line up with
    // the captured image the host displays.
    //
    // Buttons-only (no title bar rect): UIA's TitleBar element
    // reports a zero-sized bounding rectangle on modern Win11 apps
    // that use ``DwmExtendFrameIntoClientArea``, AND apps with
    // tab strips / address bars on the caption row would make any
    // synthesised title-bar rect wrongly include those as caption.
    // Caption-drag is decided per-press via ``WM_NCHITTEST`` on the
    // host side instead. Empty rect on each slot means "this button
    // isn't present on the window".
    [[nodiscard]] CaptionButtons get_caption_buttons(std::uintptr_t hwnd,
                                                       int efb_left,
                                                       int efb_top);

    // ---- Phase 8: synchronous UIA queries -------------------------------

    // Window state via the WindowPattern. ``visual_state`` is one of
    // ``"normal"`` / ``"maximized"`` / ``"minimized"`` / ``"unknown"``;
    // ``is_modal`` / ``is_topmost`` come from the same pattern's
    // direct properties.
    struct WindowState {
        bool present = false;       // WindowPattern available?
        std::string visual_state;
        bool is_modal = false;
        bool is_topmost = false;
    };
    [[nodiscard]] WindowState get_window_state(std::uintptr_t hwnd);

    // Programmatic click: find an element under ``hwnd`` whose
    // AutomationId OR Name matches ``selector`` and invoke its
    // InvokePattern. Returns true on success. Searches by
    // AutomationId first, falls back to Name.
    [[nodiscard]] bool invoke_by_selector(std::uintptr_t hwnd,
                                            const std::string& selector);

    // Set a ValuePattern-supporting element's value.
    [[nodiscard]] bool set_value(std::uintptr_t hwnd,
                                  const std::string& selector,
                                  const std::string& value);

    // Concatenate all text under ``hwnd`` via TextPattern.
    // ``max_chars`` caps the returned string to keep the wire payload
    // bounded. Empty on failure or apps with no TextPattern.
    [[nodiscard]] std::string get_text(std::uintptr_t hwnd, int max_chars);

    // Walk the UIA tree under ``hwnd`` and produce a JSON object
    // describing each element's ControlType / Name / AutomationId /
    // BoundingRectangle. ``max_depth`` clamps recursion depth.
    [[nodiscard]] nlohmann::json dump_tree(std::uintptr_t hwnd,
                                            int max_depth);

    // ---- Phase 9: event subscriptions ----------------------------------

    // Watch ``pid`` for window-opened / window-closed events. UIA
    // fires these on its own COM thread; the handler enqueues
    // ``uia_window_opened`` / ``uia_window_closed`` events into
    // ``pending_`` which the transport drains on its tick. Returns
    // a non-zero subscription id (passed to ``unsubscribe`` later)
    // or 0 on failure. Replaces the manager's ~50 Hz ``list_windows``
    // poll for the launched-app PID.
    [[nodiscard]] std::uint64_t subscribe_pid_lifecycle(std::uint32_t pid);

    // Watch focus changes within ``pid``'s windows. UIA's
    // ``IUIAutomationFocusChangedEventHandler`` fires for any focus
    // change in the desktop; the handler filters by ProcessId and
    // posts ``uia_focus_changed`` events with the new focused
    // element's HWND / AutomationId / Name. Useful for the host to
    // know which guest control has keyboard focus.
    [[nodiscard]] std::uint64_t subscribe_pid_focus(std::uint32_t pid);

    // Watch ``hwnd``'s WindowVisualState / Name / IsModal properties
    // for changes. Fires ``uia_state_changed`` / ``uia_title_changed``
    // / ``uia_modal_changed`` events. The host uses these to keep its
    // mirror chrome in sync with guest-side window state changes
    // (e.g. an app maximising itself or renaming after a save).
    [[nodiscard]] std::uint64_t subscribe_window_properties(
        std::uintptr_t hwnd);

    // Remove a subscription installed by ``subscribe_*``. No-op for
    // unknown ids. The corresponding COM event handler is detached
    // and Released here so events stop firing.
    void unsubscribe(std::uint64_t sub_id);

    // EventSource: drain any UIA-sourced push events accumulated by
    // background COM threads since the last call. Phase 6 always
    // returns empty; Phase 9 fills this in.
    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

    // Called by the COM event handler from a UIA thread when a
    // subscribed window opens or closes. Public so the friend-free
    // event-handler class in uia.cpp can post into ``pending_``
    // without touching internals directly.
    void post_event(rpc::Event ev);

private:
    // Each ``Subscription`` records what's needed to detach the
    // matching COM event handler. The agent doesn't try to be clever
    // with a single polymorphic handle; ``kind`` tells ``unsubscribe``
    // which ``Remove*EventHandler`` overload to call. ``scope`` is
    // only used for lifecycle (it's the desktop root we passed in);
    // focus handlers are process-wide so they don't need one;
    // property-change handlers attach to the target ``hwnd``'s
    // element directly, which is captured in ``scope`` too.
    enum class Kind { Lifecycle, Focus, Property };
    struct Subscription {
        Kind kind = Kind::Lifecycle;
        void* handler = nullptr;   // IUIAutomation*EventHandler*, COM-owned
        IUIAutomationElement* scope = nullptr;  // element handler is bound to
    };

    IUIAutomation* root_ = nullptr;  // COM ref-counted
    std::mutex mu_;
    std::deque<rpc::Event> pending_;
    std::mutex subs_mu_;
    std::unordered_map<std::uint64_t, Subscription> subs_;
    std::uint64_t next_sub_id_ = 1;
};

}  // namespace lsw::agent::uia
