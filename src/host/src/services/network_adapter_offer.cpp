#include "lsw/host/services/network_adapter_offer.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/services/guest_update_status.hpp"
#include "lsw/host/services/status_hub.hpp"

namespace lsw::host::services {

namespace {
using K = config::ManagerSettings::Key;
using Adapter = config::ManagerSettings::NetworkAdapter;

constexpr QLatin1StringView kDriverStep{"network driver"};
constexpr QLatin1StringView kDone{"done"};
}

NetworkAdapterOffer::NetworkAdapterOffer(StatusHub& status, config::ManagerSettings& settings,
                                         config::PendingChanges& pending, QObject* parent)
    : QObject(parent), status_(status), settings_(settings), pending_(pending) {
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area == StatusHub::Area::Update) {
            offer();
        }
    });
}

bool NetworkAdapterOffer::driver_installed(const GuestUpdateStatus& update) {
    for (const auto& step : update.last_steps) {
        if (step.name == kDriverStep) {
            return step.status == kDone;
        }
    }
    return false;
}

void NetworkAdapterOffer::offer() {
    if (settings_.bool_value(K::kNetworkAdapterOffered) ||
        settings_.string_value(K::kNetworkAdapter) != Adapter::kEmulated ||
        pending_.keys().contains(K::kNetworkAdapter) || !driver_installed(status_.update())) {
        return;
    }
    LSW_INFO("network: Windows has the virtio network driver; the virtio adapter waits for a restart");
    settings_.set_value(K::kNetworkAdapterOffered, true);
    pending_.stage(K::kNetworkAdapter, QString{Adapter::kVirtio});
}

}
