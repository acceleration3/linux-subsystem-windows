#pragma once

#include <chrono>

namespace lsw::protocol {

// How a peer is told that something was written.
//
// Waking someone across a VM boundary is the one thing shared memory
// does not give you for free, and the mechanisms are asymmetric: a guest
// can raise an MSI-X doorbell at the host, but the host has no interrupt
// back — the manager's ivshmem server holds only its own eventfds. Two
// processes on one machine, meanwhile, could use an eventfd both ways.
//
// So notification is a separate, replaceable concern, and the baseline
// implementation is the one that works everywhere: poll. ``ring`` does
// nothing, ``wait`` spins briefly and then sleeps. Interrupt-backed
// implementations are an optimisation that slots in per direction,
// wherever one happens to exist.
class Doorbell {
public:
    virtual ~Doorbell() = default;

    Doorbell() = default;
    Doorbell(const Doorbell&) = delete;
    Doorbell& operator=(const Doorbell&) = delete;
    Doorbell(Doorbell&&) = delete;
    Doorbell& operator=(Doorbell&&) = delete;

    // Tell the peer there is something to read. Must be cheap: it runs
    // on every message.
    virtual void ring() noexcept = 0;

    // Block until woken or until ``timeout`` elapses. A polling
    // implementation may return early and often — callers must re-check
    // their own condition, never assume a wakeup means data.
    //
    // ``allow_spin`` is the caller saying whether low latency is still
    // worth CPU. A busy channel wants the spin; a channel that has been
    // quiet for a while does not, and saying so is the difference
    // between an idle server costing nothing and costing a permanent
    // slice of a core. Only the caller knows which it is — the doorbell
    // cannot see the condition being waited on.
    virtual void wait(std::chrono::microseconds timeout, bool allow_spin = true) noexcept = 0;
};

// Notification by not notifying: the reader polls.
//
// Correct everywhere, needs nothing from either kernel, and for the
// latency that matters here — a thread already blocked waiting for a
// reply — a short spin beats a syscall round trip anyway. The sleep
// after the spin is what keeps an idle channel from burning a core.
class SpinPollDoorbell final : public Doorbell {
public:
    // ``spin`` is how long ``wait`` busy-polls before it starts
    // sleeping in ``sleep_step`` slices.
    explicit SpinPollDoorbell(std::chrono::microseconds spin = std::chrono::microseconds{200},
                              std::chrono::microseconds sleep_step = std::chrono::microseconds{50})
        : spin_(spin), sleep_step_(sleep_step) {}

    void ring() noexcept override {}
    void wait(std::chrono::microseconds timeout, bool allow_spin = true) noexcept override;

private:
    std::chrono::microseconds spin_;
    std::chrono::microseconds sleep_step_;
};

}  // namespace lsw::protocol
