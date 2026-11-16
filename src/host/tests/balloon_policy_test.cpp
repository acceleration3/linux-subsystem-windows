#include "lsw/host/platform/host_memory.hpp"
#include "lsw/host/services/balloon_policy.hpp"

#include <cstdio>

using lsw::host::services::BalloonPolicy;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

constexpr qint64 kGib = BalloonPolicy::kGib;

BalloonPolicy::Reading host_with(qint64 available_gib, qint64 actual_gib) {
    BalloonPolicy::Reading reading;
    reading.host_total_kib = 31 * kGib;
    reading.host_available_kib = available_gib * kGib;
    reading.guest_max_kib = 20 * kGib;
    reading.guest_actual_kib = actual_gib * kGib;
    reading.guest_usable_kib = 8 * kGib;
    return reading;
}

void check_marks() {
    check(BalloonPolicy::low_kib(31 * kGib) > 31 * kGib / 16 + kGib,
          "the guest is shrunk well before the host has as little left as the GPU's own allocations stop at");
}

void check_shrink() {
    const auto target = BalloonPolicy::target_kib(host_with(1, 20));
    const qint64 low = BalloonPolicy::low_kib(31 * kGib);
    check(target && *target == 20 * kGib - (2 * low - 1 * kGib),
          "a host short of memory takes back enough of the guest's to have twice its low mark again");

    BalloonPolicy::Reading busy = host_with(1, 20);
    busy.guest_usable_kib = 2 * kGib;
    const auto kept = BalloonPolicy::target_kib(busy);
    check(kept && *kept == 20 * kGib - 2 * kGib + BalloonPolicy::kGuestHeadroomKib,
          "but never what Windows uses, and a headroom over it");

    BalloonPolicy::Reading unknown = host_with(0, 6);
    unknown.guest_usable_kib.reset();
    const auto floor = BalloonPolicy::target_kib(unknown);
    check(floor && *floor == 5 * kGib, "nor below a quarter of the guest's size when Windows says nothing");

    check(!BalloonPolicy::target_kib(host_with(1, 5)), "a guest already at its floor is left");
}

void check_grow() {
    BalloonPolicy::Reading starved = host_with(12, 10);
    starved.guest_usable_kib = 512 * 1024;
    check(BalloonPolicy::target_kib(starved) == 10 * kGib + BalloonPolicy::kGuestGrowKib,
          "a Windows short of memory gets more, a step at a time");
    starved.guest_actual_kib = 19 * kGib;
    check(BalloonPolicy::target_kib(starved) == 20 * kGib, "never more than the guest's size");
    BalloonPolicy::Reading tight = host_with(5, 10);
    tight.guest_usable_kib = 512 * 1024;
    check(BalloonPolicy::target_kib(tight) == 10 * kGib + 5 * kGib - BalloonPolicy::low_kib(31 * kGib),
          "and only what the host has over its low mark");

    BalloonPolicy::Reading silent = host_with(12, 10);
    silent.guest_usable_kib.reset();
    check(BalloonPolicy::target_kib(silent) == 10 * kGib + (12 * kGib - 2 * BalloonPolicy::low_kib(31 * kGib)),
          "a Windows that says nothing gets back what the host has over twice its low mark");
}

void check_reclaim() {
    BalloonPolicy::Reading idle = host_with(12, 14);
    idle.guest_usable_kib = 10 * kGib;
    check(BalloonPolicy::target_kib(idle) == 14 * kGib - 8 * kGib,
          "memory Windows does not use goes back to the host, though the host has room, leaving it a spare");
    BalloonPolicy::Reading barely = host_with(12, 14);
    barely.guest_usable_kib = 3 * kGib;
    check(!BalloonPolicy::target_kib(barely), "a spare within its slack is left");
}

void check_steady() {
    BalloonPolicy::Reading settled = host_with(5, 14);
    settled.guest_usable_kib = 2 * kGib;
    check(!BalloonPolicy::target_kib(settled), "a Windows using what it has, on a host with room, is left");
}

void check_host() {
    using lsw::host::platform::HostMemory;
    const auto total = HostMemory::total_mib();
    const auto available = HostMemory::available_mib();
    check(total && *total > 0 && available && *available > 0 && *available <= *total,
          "the host's memory and what it has available are read from /proc/meminfo");
}

}

int main() {
    check_host();
    check_marks();
    check_shrink();
    check_grow();
    check_reclaim();
    check_steady();
    std::printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
