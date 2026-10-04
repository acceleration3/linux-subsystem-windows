#pragma once

namespace lsw::agent::win {

// RAII wrapper for ``CoInitializeEx`` / ``CoUninitialize`` on the
// thread that constructs the object.
//
// The default is MTA because that's what IUIAutomation expects — UIA
// clients in an STA serialise all calls through the originating thread,
// which we don't want for a service that runs UIA event handlers on the
// COM pool. The shell's objects (IShellLink) want an STA, which callers
// on a thread of their own ask for explicitly.
//
// Construct once per thread (or per call, on a thread that does not
// otherwise use COM). The agent does this on the main thread at startup;
// UIA event-handler threads (managed inside the COM pool by Windows)
// initialise themselves.
//
// On failure ``ok()`` returns false; destruction is still safe
// (becomes a no-op).
class Apartment {
public:
    enum class Model { Multithreaded, SingleThreaded };

    explicit Apartment(Model model = Model::Multithreaded);
    ~Apartment();
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;

    [[nodiscard]] bool ok() const noexcept { return initialised_; }

private:
    bool initialised_ = false;
};

}  // namespace lsw::agent::win
