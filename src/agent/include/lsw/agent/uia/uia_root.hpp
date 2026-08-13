#pragma once

struct IUIAutomation;

namespace lsw::agent::uia {

// Owns the process-wide ``IUIAutomation`` object.
//
// Everything else in this directory borrows the raw pointer from here
// and never manages its lifetime. That split matters because the release
// is ordered: it must happen while the COM apartment is still alive, and
// the apartment outlives every query and subscription object.
class UiaRoot {
public:
    // Creates the IUIAutomation instance. Requires COM to already be
    // initialised on this thread (AgentServices guarantees that by
    // member order). Failure is not fatal — ``ok()`` reports it and
    // every caller degrades to a no-op.
    UiaRoot();
    ~UiaRoot();

    UiaRoot(const UiaRoot&) = delete;
    UiaRoot& operator=(const UiaRoot&) = delete;

    [[nodiscard]] bool ok() const noexcept { return root_ != nullptr; }

    // Borrowed. Null when UIA is unavailable — callers must check.
    [[nodiscard]] IUIAutomation* get() const noexcept { return root_; }

    // Release early, before the COM apartment goes away. Idempotent.
    void shutdown();

private:
    IUIAutomation* root_ = nullptr;  // COM ref-counted
};

}  // namespace lsw::agent::uia
