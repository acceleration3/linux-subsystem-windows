#include "lsw/agent/uia/uia.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>
// clang-format on

#include <atomic>  // used by event-handler COM objects' ref-count

namespace lsw::agent::uia {

Uia::Uia() {
    // Lazy creation guarded by COM init — Apartment is constructed
    // before Uia by ``AgentServices`` (member order), so by the time
    // we get here MTA is live on this thread.
    IUIAutomation* root = nullptr;
    const HRESULT hr = ::CoCreateInstance(
        __uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IUIAutomation), reinterpret_cast<void**>(&root));
    if (FAILED(hr) || root == nullptr) {
        LSW_AGENT_WARN(std::string{"uia: CoCreateInstance(CUIAutomation) failed hr=0x"}
                        + std::to_string(static_cast<unsigned long>(hr))
                        + " — UIA RPCs will be unavailable");
    } else {
        root_ = root;
        LSW_AGENT_INFO("uia: IUIAutomation ready");
    }
}

Uia::~Uia() {
    shutdown();
}

void Uia::shutdown() {
    if (root_ != nullptr) {
        root_->Release();
        root_ = nullptr;
    }
}

namespace {

// Convert a UIA bounding rectangle (screen coords, doubles) into an
// EFB-relative integer rect, clamped to non-negative width/height.
void fill_efb_rect(int& dx, int& dy, int& dw, int& dh,
                   const RECT& screen, int efb_left, int efb_top) {
    const int w = screen.right - screen.left;
    const int h = screen.bottom - screen.top;
    if (w <= 0 || h <= 0) {
        dx = dy = dw = dh = 0;
        return;
    }
    dx = screen.left - efb_left;
    dy = screen.top - efb_top;
    dw = w;
    dh = h;
}

// Find the caption Button child whose AutomationId OR Name matches
// ``key``. Modern Win11 apps (WindowsTerminal, Notepad, …) report
// EMPTY AutomationId on the system caption buttons and identify
// them only by Name — so AutomationId-only matching never finds
// them. Try AutomationId first (classic Win32 apps set it), then
// fall back to Name. Caller Releases.
IUIAutomationElement* find_caption_button(IUIAutomation* root,
                                            IUIAutomationElement* parent,
                                            const wchar_t* key) {
    if (root == nullptr || parent == nullptr || key == nullptr) {
        return nullptr;
    }
    auto find_by = [&](PROPERTYID prop) -> IUIAutomationElement* {
        VARIANT v;
        ::VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = ::SysAllocString(key);
        IUIAutomationCondition* cond = nullptr;
        IUIAutomationElement* found = nullptr;
        if (SUCCEEDED(root->CreatePropertyCondition(prop, v, &cond))
            && cond != nullptr) {
            parent->FindFirst(TreeScope_Descendants, cond, &found);
            cond->Release();
        }
        ::VariantClear(&v);
        return found;
    };
    if (auto* e = find_by(UIA_AutomationIdPropertyId); e != nullptr) {
        return e;
    }
    return find_by(UIA_NamePropertyId);
}

}  // namespace

CaptionButtons Uia::get_caption_buttons(std::uintptr_t hwnd_raw,
                                          int efb_left, int efb_top) {
    CaptionButtons out;
    if (root_ == nullptr) {
        return out;
    }
    auto hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr) {
        return out;
    }
    IUIAutomationElement* elem = nullptr;
    if (FAILED(root_->ElementFromHandle(hwnd, &elem)) || elem == nullptr) {
        return out;
    }

    // Try the classic path first: a ``UIA_TitleBarControlTypeId``
    // element under the window. Most Win32 / WinUI apps expose this,
    // and limiting the button search to its subtree avoids false
    // positives from deeper toolbar buttons (e.g. tab-close).
    //
    // Fallback (Edge / Chrome / fully custom Chromium chrome): no
    // TitleBar exists — buttons live under a generic Pane deeper in
    // the tree. Search the window subtree directly. FindFirst's
    // document order returns the top-most match first, which is the
    // title-bar button (the in-content "Close tab" / dialog Close
    // are later in the tree).
    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = UIA_TitleBarControlTypeId;
    IUIAutomationCondition* tb_cond = nullptr;
    IUIAutomationElement* tb = nullptr;
    if (SUCCEEDED(root_->CreatePropertyCondition(UIA_ControlTypePropertyId, v,
                                                  &tb_cond))
        && tb_cond != nullptr) {
        elem->FindFirst(TreeScope_Descendants, tb_cond, &tb);
        tb_cond->Release();
    }
    ::VariantClear(&v);
    IUIAutomationElement* scope = (tb != nullptr) ? tb : elem;

    // The max button is labelled "Maximize" in normal state and
    // "Restore" once the window is maximised. Some apps also use
    // "Restore Down". Try each in order and stop on the first hit.
    auto fill_first_match = [&](IUIAutomationElement* parent,
                                 std::initializer_list<const wchar_t*> names,
                                 int& x, int& y, int& w, int& h) {
        for (const auto* name : names) {
            IUIAutomationElement* btn =
                find_caption_button(root_, parent, name);
            if (btn == nullptr) continue;
            RECT br{};
            if (SUCCEEDED(btn->get_CurrentBoundingRectangle(&br))) {
                fill_efb_rect(x, y, w, h, br, efb_left, efb_top);
            }
            btn->Release();
            return;
        }
    };
    fill_first_match(scope, {L"Minimize"}, out.min_x, out.min_y,
                     out.min_w, out.min_h);
    fill_first_match(scope, {L"Maximize", L"Restore", L"Restore Down"},
                     out.max_x, out.max_y, out.max_w, out.max_h);
    fill_first_match(scope, {L"Close"}, out.close_x, out.close_y,
                     out.close_w, out.close_h);
    fill_first_match(scope, {L"Help"}, out.help_x, out.help_y,
                     out.help_w, out.help_h);

    if (tb != nullptr) tb->Release();
    elem->Release();
    return out;
}

namespace {

// UTF-8 → UTF-16 via Win32 ``MultiByteToWideChar``. Returns empty on
// empty input or failure. Used wherever a selector / value string
// crosses into a COM ``BSTR``.
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                          static_cast<int>(s.size()),
                                          nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                          static_cast<int>(s.size()), w.data(), n);
    return w;
}

// UTF-16 → UTF-8 for ``BSTR`` returned by UIA properties.
std::string narrow(BSTR b) {
    if (b == nullptr) return {};
    const int wlen = static_cast<int>(::SysStringLen(b));
    if (wlen <= 0) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, b, wlen, nullptr, 0,
                                          nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, b, wlen, s.data(), n, nullptr, nullptr);
    return s;
}

// Find an element under ``parent`` whose AutomationId or Name matches
// ``selector``. Tries AutomationId first (more stable), falls back
// to Name. Caller Releases the returned pointer.
IUIAutomationElement* find_by_selector(IUIAutomation* root,
                                        IUIAutomationElement* parent,
                                        const std::wstring& selector) {
    if (root == nullptr || parent == nullptr || selector.empty()) {
        return nullptr;
    }
    auto find_by_prop = [&](PROPERTYID prop) -> IUIAutomationElement* {
        VARIANT v;
        ::VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = ::SysAllocString(selector.c_str());
        IUIAutomationCondition* cond = nullptr;
        IUIAutomationElement* found = nullptr;
        if (SUCCEEDED(root->CreatePropertyCondition(prop, v, &cond))
            && cond != nullptr) {
            parent->FindFirst(TreeScope_Descendants, cond, &found);
            cond->Release();
        }
        ::VariantClear(&v);
        return found;
    };
    if (auto* e = find_by_prop(UIA_AutomationIdPropertyId); e != nullptr) {
        return e;
    }
    return find_by_prop(UIA_NamePropertyId);
}

}  // namespace

Uia::WindowState Uia::get_window_state(std::uintptr_t hwnd_raw) {
    WindowState out;
    if (root_ == nullptr) return out;
    auto hwnd = reinterpret_cast<HWND>(hwnd_raw);
    IUIAutomationElement* elem = nullptr;
    if (FAILED(root_->ElementFromHandle(hwnd, &elem)) || elem == nullptr) {
        return out;
    }
    IUIAutomationWindowPattern* wp = nullptr;
    elem->GetCurrentPatternAs(UIA_WindowPatternId,
                              __uuidof(IUIAutomationWindowPattern),
                              reinterpret_cast<void**>(&wp));
    elem->Release();
    if (wp == nullptr) return out;
    out.present = true;
    WindowVisualState vs = WindowVisualState_Normal;
    if (SUCCEEDED(wp->get_CurrentWindowVisualState(&vs))) {
        switch (vs) {
        case WindowVisualState_Normal:    out.visual_state = "normal";    break;
        case WindowVisualState_Maximized: out.visual_state = "maximized"; break;
        case WindowVisualState_Minimized: out.visual_state = "minimized"; break;
        }
    }
    BOOL b = FALSE;
    if (SUCCEEDED(wp->get_CurrentIsModal(&b))) {
        out.is_modal = b != FALSE;
    }
    if (SUCCEEDED(wp->get_CurrentIsTopmost(&b))) {
        out.is_topmost = b != FALSE;
    }
    wp->Release();
    return out;
}

bool Uia::invoke_by_selector(std::uintptr_t hwnd_raw,
                              const std::string& selector) {
    if (root_ == nullptr) return false;
    auto hwnd = reinterpret_cast<HWND>(hwnd_raw);
    IUIAutomationElement* root_elem = nullptr;
    if (FAILED(root_->ElementFromHandle(hwnd, &root_elem))
        || root_elem == nullptr) {
        return false;
    }
    IUIAutomationElement* target =
        find_by_selector(root_, root_elem, widen(selector));
    root_elem->Release();
    if (target == nullptr) {
        return false;
    }
    IUIAutomationInvokePattern* ip = nullptr;
    target->GetCurrentPatternAs(UIA_InvokePatternId,
                                __uuidof(IUIAutomationInvokePattern),
                                reinterpret_cast<void**>(&ip));
    target->Release();
    if (ip == nullptr) return false;
    const HRESULT hr = ip->Invoke();
    ip->Release();
    return SUCCEEDED(hr);
}

bool Uia::set_value(std::uintptr_t hwnd_raw, const std::string& selector,
                     const std::string& value) {
    if (root_ == nullptr) return false;
    auto hwnd = reinterpret_cast<HWND>(hwnd_raw);
    IUIAutomationElement* root_elem = nullptr;
    if (FAILED(root_->ElementFromHandle(hwnd, &root_elem))
        || root_elem == nullptr) {
        return false;
    }
    IUIAutomationElement* target =
        find_by_selector(root_, root_elem, widen(selector));
    root_elem->Release();
    if (target == nullptr) return false;
    IUIAutomationValuePattern* vp = nullptr;
    target->GetCurrentPatternAs(UIA_ValuePatternId,
                                __uuidof(IUIAutomationValuePattern),
                                reinterpret_cast<void**>(&vp));
    target->Release();
    if (vp == nullptr) return false;
    BSTR bs = ::SysAllocString(widen(value).c_str());
    const HRESULT hr = vp->SetValue(bs);
    ::SysFreeString(bs);
    vp->Release();
    return SUCCEEDED(hr);
}

std::string Uia::get_text(std::uintptr_t hwnd_raw, int max_chars) {
    if (root_ == nullptr) return {};
    auto hwnd = reinterpret_cast<HWND>(hwnd_raw);
    IUIAutomationElement* elem = nullptr;
    if (FAILED(root_->ElementFromHandle(hwnd, &elem)) || elem == nullptr) {
        return {};
    }
    IUIAutomationTextPattern* tp = nullptr;
    elem->GetCurrentPatternAs(UIA_TextPatternId,
                              __uuidof(IUIAutomationTextPattern),
                              reinterpret_cast<void**>(&tp));
    elem->Release();
    if (tp == nullptr) return {};
    IUIAutomationTextRange* range = nullptr;
    tp->get_DocumentRange(&range);
    tp->Release();
    if (range == nullptr) return {};
    BSTR text = nullptr;
    const int cap = max_chars > 0 ? max_chars : -1;
    range->GetText(cap, &text);
    range->Release();
    std::string out = narrow(text);
    if (text != nullptr) {
        ::SysFreeString(text);
    }
    return out;
}

namespace {

void dump_one(IUIAutomationTreeWalker* walker, IUIAutomationElement* elem,
              int depth, int max_depth, nlohmann::json& out) {
    if (elem == nullptr || depth > max_depth) return;
    CONTROLTYPEID ctype = 0;
    elem->get_CurrentControlType(&ctype);
    BSTR name = nullptr;
    BSTR aid = nullptr;
    elem->get_CurrentName(&name);
    elem->get_CurrentAutomationId(&aid);
    RECT br{};
    elem->get_CurrentBoundingRectangle(&br);
    nlohmann::json node = {
        {"control_type", static_cast<int>(ctype)},
        {"name", narrow(name)},
        {"automation_id", narrow(aid)},
        {"rect", {br.left, br.top, br.right - br.left, br.bottom - br.top}},
    };
    if (name) ::SysFreeString(name);
    if (aid)  ::SysFreeString(aid);

    nlohmann::json kids = nlohmann::json::array();
    if (depth < max_depth) {
        IUIAutomationElement* child = nullptr;
        walker->GetFirstChildElement(elem, &child);
        while (child != nullptr) {
            nlohmann::json child_json;
            dump_one(walker, child, depth + 1, max_depth, child_json);
            kids.push_back(std::move(child_json));
            IUIAutomationElement* sib = nullptr;
            walker->GetNextSiblingElement(child, &sib);
            child->Release();
            child = sib;
        }
    }
    node["children"] = std::move(kids);
    out = std::move(node);
}

}  // namespace

nlohmann::json Uia::dump_tree(std::uintptr_t hwnd_raw, int max_depth) {
    nlohmann::json out = nlohmann::json::object();
    if (root_ == nullptr) return out;
    auto hwnd = reinterpret_cast<HWND>(hwnd_raw);
    IUIAutomationElement* elem = nullptr;
    if (FAILED(root_->ElementFromHandle(hwnd, &elem)) || elem == nullptr) {
        return out;
    }
    IUIAutomationTreeWalker* walker = nullptr;
    root_->get_ControlViewWalker(&walker);
    if (walker == nullptr) {
        elem->Release();
        return out;
    }
    dump_one(walker, elem, 0, max_depth > 0 ? max_depth : 4, out);
    walker->Release();
    elem->Release();
    return out;
}

void Uia::post_event(rpc::Event ev) {
    std::lock_guard<std::mutex> g(mu_);
    // Cap the queue so a runaway notifier (e.g. an installer window
    // briefly flashing thousands of child popups) can't grow the
    // process unboundedly. 4096 entries ≈ a few seconds of pathological
    // activity; older events are dropped first.
    constexpr size_t kCap = 4096;
    if (pending_.size() >= kCap) {
        pending_.pop_front();
    }
    pending_.push_back(std::move(ev));
}

std::vector<rpc::Event> Uia::collect_pending() {
    std::vector<rpc::Event> out;
    std::lock_guard<std::mutex> g(mu_);
    if (pending_.empty()) {
        return out;
    }
    out.reserve(pending_.size());
    for (auto& e : pending_) {
        out.push_back(std::move(e));
    }
    pending_.clear();
    return out;
}

// ---- Phase 9: lifecycle event subscription ----------------------------------

namespace {

// COM event handler for ``UIA_Window_WindowOpenedEventId`` and
// ``UIA_Window_WindowClosedEventId``. UIA invokes
// ``HandleAutomationEvent`` on its own thread; we read a few
// properties off the element and post a JSON payload into the
// ``Uia::pending_`` queue. Filtering by PID happens at subscription
// time (passed in CreateCacheRequest's scope) — but UIA also fires
// for descendants of the launched root, so we additionally check
// the element's ``ProcessId`` here.
class LifecycleHandler final : public IUIAutomationEventHandler {
public:
    LifecycleHandler(lsw::agent::uia::Uia* owner, DWORD pid)
        : owner_(owner), pid_(pid) {}

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (iid == __uuidof(IUnknown)
            || iid == __uuidof(IUIAutomationEventHandler)) {
            *out = static_cast<IUIAutomationEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return ++ref_; }
    ULONG __stdcall Release() override {
        const ULONG r = --ref_;
        if (r == 0) {
            delete this;
        }
        return r;
    }

    HRESULT __stdcall HandleAutomationEvent(IUIAutomationElement* sender,
                                              EVENTID eid) override {
        if (owner_ == nullptr || sender == nullptr) return S_OK;
        int sender_pid = 0;
        sender->get_CurrentProcessId(&sender_pid);
        if (static_cast<DWORD>(sender_pid) != pid_) {
            return S_OK;  // not our launched app's tree
        }
        UIA_HWND hwnd_raw = nullptr;
        sender->get_CurrentNativeWindowHandle(&hwnd_raw);
        const auto hwnd_u64 = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(hwnd_raw));
        if (eid == UIA_Window_WindowClosedEventId) {
            owner_->post_event({"uia_window_closed", {{"hwnd", hwnd_u64}}});
            return S_OK;
        }
        // Opened: include title + control type so the host can decide
        // which mirror to spawn without an extra round-trip.
        BSTR name = nullptr;
        sender->get_CurrentName(&name);
        CONTROLTYPEID ctype = 0;
        sender->get_CurrentControlType(&ctype);
        const int n = name == nullptr ? 0 : static_cast<int>(::SysStringLen(name));
        std::string title;
        if (n > 0) {
            const int u8 = ::WideCharToMultiByte(CP_UTF8, 0, name, n, nullptr,
                                                  0, nullptr, nullptr);
            if (u8 > 0) {
                title.resize(static_cast<size_t>(u8));
                ::WideCharToMultiByte(CP_UTF8, 0, name, n, title.data(), u8,
                                      nullptr, nullptr);
            }
        }
        if (name != nullptr) ::SysFreeString(name);
        owner_->post_event({"uia_window_opened",
                            {{"hwnd", hwnd_u64},
                             {"title", title},
                             {"control_type", static_cast<int>(ctype)},
                             {"pid", static_cast<std::uint64_t>(pid_)}}});
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
    lsw::agent::uia::Uia* owner_;
    DWORD pid_;
};

}  // namespace

std::uint64_t Uia::subscribe_pid_lifecycle(std::uint32_t pid) {
    if (root_ == nullptr) return 0;
    IUIAutomationElement* desktop = nullptr;
    if (FAILED(root_->GetRootElement(&desktop)) || desktop == nullptr) {
        return 0;
    }
    auto* handler = new LifecycleHandler(this, static_cast<DWORD>(pid));
    // Subtree scope: catches the root window AND any popups / dialogs
    // the app spawns later. PID filtering happens in
    // HandleAutomationEvent above.
    HRESULT hr_open = root_->AddAutomationEventHandler(
        UIA_Window_WindowOpenedEventId, desktop, TreeScope_Subtree, nullptr,
        handler);
    HRESULT hr_close = root_->AddAutomationEventHandler(
        UIA_Window_WindowClosedEventId, desktop, TreeScope_Subtree, nullptr,
        handler);
    if (FAILED(hr_open) || FAILED(hr_close)) {
        if (SUCCEEDED(hr_open)) {
            root_->RemoveAutomationEventHandler(
                UIA_Window_WindowOpenedEventId, desktop, handler);
        }
        if (SUCCEEDED(hr_close)) {
            root_->RemoveAutomationEventHandler(
                UIA_Window_WindowClosedEventId, desktop, handler);
        }
        handler->Release();
        desktop->Release();
        LSW_AGENT_WARN("uia: AddAutomationEventHandler failed");
        return 0;
    }
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> g(subs_mu_);
        id = next_sub_id_++;
        subs_.emplace(id, Subscription{Kind::Lifecycle, handler, desktop});
    }
    return id;
}

// ---- Phase 9: focus subscription ---------------------------------------------

namespace {

// IUIAutomationFocusChangedEventHandler is process-wide — it fires
// for any focus change in the desktop. We filter by ``ProcessId`` in
// HandleFocusChangedEvent so a single subscription can serve any
// number of host-side mirrors as long as the PID matches.
class FocusHandler final : public IUIAutomationFocusChangedEventHandler {
public:
    FocusHandler(lsw::agent::uia::Uia* owner, DWORD pid)
        : owner_(owner), pid_(pid) {}

    HRESULT __stdcall QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (iid == __uuidof(IUnknown)
            || iid == __uuidof(IUIAutomationFocusChangedEventHandler)) {
            *out = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return ++ref_; }
    ULONG __stdcall Release() override {
        const ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }

    HRESULT __stdcall HandleFocusChangedEvent(
        IUIAutomationElement* sender) override {
        if (owner_ == nullptr || sender == nullptr) return S_OK;
        int sender_pid = 0;
        sender->get_CurrentProcessId(&sender_pid);
        if (static_cast<DWORD>(sender_pid) != pid_) return S_OK;
        UIA_HWND hwnd_raw = nullptr;
        sender->get_CurrentNativeWindowHandle(&hwnd_raw);
        BSTR name = nullptr, aid = nullptr;
        sender->get_CurrentName(&name);
        sender->get_CurrentAutomationId(&aid);
        auto to_utf8 = [](BSTR b) {
            if (b == nullptr) return std::string{};
            const int wlen = static_cast<int>(::SysStringLen(b));
            if (wlen <= 0) return std::string{};
            const int n = ::WideCharToMultiByte(CP_UTF8, 0, b, wlen, nullptr,
                                                  0, nullptr, nullptr);
            if (n <= 0) return std::string{};
            std::string s(static_cast<size_t>(n), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, b, wlen, s.data(), n, nullptr,
                                  nullptr);
            return s;
        };
        owner_->post_event(
            {"uia_focus_changed",
             {{"hwnd", static_cast<std::uint64_t>(
                          reinterpret_cast<std::uintptr_t>(hwnd_raw))},
              {"name", to_utf8(name)},
              {"automation_id", to_utf8(aid)}}});
        if (name != nullptr) ::SysFreeString(name);
        if (aid != nullptr)  ::SysFreeString(aid);
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
    lsw::agent::uia::Uia* owner_;
    DWORD pid_;
};

}  // namespace

std::uint64_t Uia::subscribe_pid_focus(std::uint32_t pid) {
    if (root_ == nullptr) return 0;
    auto* handler = new FocusHandler(this, static_cast<DWORD>(pid));
    if (FAILED(root_->AddFocusChangedEventHandler(nullptr, handler))) {
        handler->Release();
        return 0;
    }
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> g(subs_mu_);
        id = next_sub_id_++;
        subs_.emplace(id, Subscription{Kind::Focus, handler, nullptr});
    }
    return id;
}

// ---- Phase 9: property-change subscription -----------------------------------

namespace {

// Watches WindowVisualState / Name / IsModal for a single ``hwnd``
// (TreeScope_Element). Fires distinct topics so the host can route
// each cheaply without re-parsing.
class PropertyHandler final : public IUIAutomationPropertyChangedEventHandler {
public:
    explicit PropertyHandler(lsw::agent::uia::Uia* owner) : owner_(owner) {}

    HRESULT __stdcall QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (iid == __uuidof(IUnknown)
            || iid == __uuidof(IUIAutomationPropertyChangedEventHandler)) {
            *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return ++ref_; }
    ULONG __stdcall Release() override {
        const ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }

    HRESULT __stdcall HandlePropertyChangedEvent(IUIAutomationElement* sender,
                                                   PROPERTYID prop,
                                                   VARIANT new_value) override {
        if (owner_ == nullptr || sender == nullptr) return S_OK;
        UIA_HWND hwnd_raw = nullptr;
        sender->get_CurrentNativeWindowHandle(&hwnd_raw);
        const std::uint64_t hwnd_u64 = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(hwnd_raw));
        if (prop == UIA_WindowWindowVisualStatePropertyId) {
            const char* s = "unknown";
            if (new_value.vt == VT_I4) {
                switch (new_value.lVal) {
                case WindowVisualState_Normal:    s = "normal";    break;
                case WindowVisualState_Maximized: s = "maximized"; break;
                case WindowVisualState_Minimized: s = "minimized"; break;
                }
            }
            owner_->post_event(
                {"uia_state_changed",
                 {{"hwnd", hwnd_u64}, {"state", std::string{s}}}});
        } else if (prop == UIA_NamePropertyId && new_value.vt == VT_BSTR) {
            BSTR b = new_value.bstrVal;
            std::string title;
            if (b != nullptr) {
                const int wlen = static_cast<int>(::SysStringLen(b));
                if (wlen > 0) {
                    const int n = ::WideCharToMultiByte(CP_UTF8, 0, b, wlen,
                                                          nullptr, 0, nullptr,
                                                          nullptr);
                    if (n > 0) {
                        title.resize(static_cast<size_t>(n));
                        ::WideCharToMultiByte(CP_UTF8, 0, b, wlen, title.data(),
                                              n, nullptr, nullptr);
                    }
                }
            }
            owner_->post_event(
                {"uia_title_changed",
                 {{"hwnd", hwnd_u64}, {"title", std::move(title)}}});
        } else if (prop == UIA_WindowIsModalPropertyId
                   && new_value.vt == VT_BOOL) {
            owner_->post_event(
                {"uia_modal_changed",
                 {{"hwnd", hwnd_u64},
                  {"is_modal", new_value.boolVal != VARIANT_FALSE}}});
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
    lsw::agent::uia::Uia* owner_;
};

}  // namespace

std::uint64_t Uia::subscribe_window_properties(std::uintptr_t hwnd_raw) {
    if (root_ == nullptr) return 0;
    auto hwnd = reinterpret_cast<HWND>(hwnd_raw);
    IUIAutomationElement* elem = nullptr;
    if (FAILED(root_->ElementFromHandle(hwnd, &elem)) || elem == nullptr) {
        return 0;
    }
    auto* handler = new PropertyHandler(this);
    const PROPERTYID props[] = {UIA_WindowWindowVisualStatePropertyId,
                                 UIA_NamePropertyId,
                                 UIA_WindowIsModalPropertyId};
    const HRESULT hr = root_->AddPropertyChangedEventHandlerNativeArray(
        elem, TreeScope_Element, nullptr, handler,
        const_cast<PROPERTYID*>(props),
        static_cast<int>(sizeof(props) / sizeof(props[0])));
    if (FAILED(hr)) {
        handler->Release();
        elem->Release();
        return 0;
    }
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> g(subs_mu_);
        id = next_sub_id_++;
        subs_.emplace(id, Subscription{Kind::Property, handler, elem});
    }
    return id;
}

// ---- Common unsubscribe ------------------------------------------------------

void Uia::unsubscribe(std::uint64_t sub_id) {
    if (sub_id == 0 || root_ == nullptr) return;
    Subscription s;
    {
        std::lock_guard<std::mutex> g(subs_mu_);
        auto it = subs_.find(sub_id);
        if (it == subs_.end()) return;
        s = it->second;
        subs_.erase(it);
    }
    if (s.handler == nullptr) return;
    switch (s.kind) {
    case Kind::Lifecycle: {
        auto* h = static_cast<IUIAutomationEventHandler*>(s.handler);
        if (s.scope != nullptr) {
            root_->RemoveAutomationEventHandler(UIA_Window_WindowOpenedEventId,
                                                 s.scope, h);
            root_->RemoveAutomationEventHandler(UIA_Window_WindowClosedEventId,
                                                 s.scope, h);
            s.scope->Release();
        }
        h->Release();
        break;
    }
    case Kind::Focus: {
        auto* h = static_cast<IUIAutomationFocusChangedEventHandler*>(s.handler);
        root_->RemoveFocusChangedEventHandler(h);
        h->Release();
        break;
    }
    case Kind::Property: {
        auto* h = static_cast<IUIAutomationPropertyChangedEventHandler*>(
            s.handler);
        if (s.scope != nullptr) {
            root_->RemovePropertyChangedEventHandler(s.scope, h);
            s.scope->Release();
        }
        h->Release();
        break;
    }
    }
}

}  // namespace lsw::agent::uia
