#include "lsw/protocol/doorbell.hpp"

#include <thread>

namespace lsw::protocol {

namespace {

// Tell the core we are in a spin loop. Not a yield — the point is to
// stay on the CPU with the peer's cache line hot, while letting a
// hyperthread sibling have the pipeline and keeping the memory-order
// machinery from mispredicting its way through the loop.
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}

}  // namespace

void SpinPollDoorbell::wait(std::chrono::microseconds timeout, bool allow_spin) noexcept {
    if (timeout <= std::chrono::microseconds::zero()) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + timeout;

    // Spin first, but only while the caller says it is worth it.
    //
    // The case the spin exists for is a thread blocked on a reply from a
    // peer actively working on it: the answer lands in microseconds and
    // a syscall round trip would cost more than the wait. The case it
    // must NOT apply to is an idle channel — a server with no client
    // polls forever, and spinning 200us out of every 4ms slice is 5% of
    // a core, permanently, measured on a server that had been idle for
    // six hours. The caller backs off its slice as a channel goes quiet
    // and stops asking for the spin at the same time.
    if (allow_spin) {
        const auto spin_until = start + (spin_ < timeout ? spin_ : timeout);
        while (std::chrono::steady_clock::now() < spin_until) {
            cpu_relax();
        }
    }

    // Then sleep out the rest, in ONE sleep.
    //
    // This used to sleep in ``sleep_step_`` slices, on the theory that
    // callers poll a condition this class cannot see and want frequent
    // wakeups. They do — but they get them from the timeout they choose,
    // not from what happens inside it: nothing can observe a wait
    // mid-flight, so waking 80 times inside a 4ms wait only bought 80
    // nanosleep syscalls instead of one. At ~20k syscalls a second that
    // was most of an idle channel's cost.
    const auto now = std::chrono::steady_clock::now();
    if (now < deadline) {
        std::this_thread::sleep_for(deadline - now);
    }
}

}  // namespace lsw::protocol
