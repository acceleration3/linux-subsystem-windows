#pragma once

namespace lsw::agent::win {

// RAII wrapper for ``CoInitializeEx`` / ``CoUninitialize`` on the
// thread that constructs the object. Apartment model is fixed to
// MTA because that's what IUIAutomation expects — UIA clients in an
// STA serialise all calls through the originating thread, which we
// don't want for a service that runs UIA event handlers on the COM
// pool. WGC's existing ``RoInitialize`` in capture::Wgc is WinRT
// (apartment-agnostic at this level) and coexists fine with our MTA
// init.
//
// Construct ONCE per thread that needs COM. The agent does this on
// the main thread at startup; UIA event-handler threads (managed
// inside the COM pool by Windows) initialise themselves.
//
// On failure ``ok()`` returns false; destruction is still safe
// (becomes a no-op).
class Apartment {
public:
    Apartment();
    ~Apartment();
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;

    [[nodiscard]] bool ok() const noexcept { return initialised_; }

private:
    bool initialised_ = false;
};

}  // namespace lsw::agent::win
