#include "lsw/host/services/balloon_controller.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/host_memory.hpp"
#include "lsw/host/services/balloon_policy.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/guest_balloon.hpp"

namespace lsw::host::services {

namespace {
constexpr qint64 kKibPerMib = 1024;
}

BalloonController::BalloonController(StatusHub& status, vm::GuestBalloon& balloon, QObject* parent)
    : QObject(parent), status_(status), balloon_(balloon) {
    connect(&timer_, &QTimer::timeout, this, &BalloonController::tick);
    timer_.start(kPeriodMs);
}

void BalloonController::tick() {
    const VmStatus& vm = status_.vm();
    if (!vm.stats.running || vm.uuid.isNull()) {
        return;
    }
    const auto guest = balloon_.read(vm.uuid);
    const auto available = platform::HostMemory::available_mib();
    const auto total = platform::HostMemory::total_mib();
    if (!guest || !available || !total || moving(guest->actual_kib)) {
        return;
    }
    const BalloonPolicy::Reading reading{*available * kKibPerMib, *total * kKibPerMib,
                                         guest->max_kib, guest->actual_kib, guest->usable_kib};
    const auto target = BalloonPolicy::target_kib(reading);
    if (!target) {
        return;
    }
    LSW_INFO("balloon: the host has {} MiB available; the guest goes from {} to {} MiB of {} "
             "(Windows can spare {})",
             *available, guest->actual_kib / kKibPerMib, *target / kKibPerMib,
             guest->max_kib / kKibPerMib,
             guest->usable_kib ? std::to_string(*guest->usable_kib / kKibPerMib) + " MiB"
                               : std::string{"an unknown amount"});
    if (balloon_.set_target(vm.uuid, *target)) {
        asked_kib_ = *target;
        asked_at_.start();
    }
}

bool BalloonController::moving(qint64 actual_kib) const {
    if (!asked_kib_ || asked_at_.elapsed() >= kMostSettleMs) {
        return false;
    }
    return qAbs(actual_kib - *asked_kib_) >= BalloonPolicy::kStepKib;
}

}  // namespace lsw::host::services
