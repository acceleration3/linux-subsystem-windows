#pragma once

#include <QObject>

namespace lsw::host::config {
class ManagerSettings;
class PendingChanges;
}  // namespace lsw::host::config

namespace lsw::host::services {

class StatusHub;
struct GuestUpdateStatus;

class NetworkAdapterOffer : public QObject {
    Q_OBJECT
public:
    NetworkAdapterOffer(StatusHub& status, config::ManagerSettings& settings,
                        config::PendingChanges& pending, QObject* parent = nullptr);

    [[nodiscard]] static bool driver_installed(const GuestUpdateStatus& update);

private:
    void offer();

    StatusHub& status_;
    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
};

}  // namespace lsw::host::services
