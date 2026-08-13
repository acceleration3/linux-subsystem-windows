#pragma once

#include "lsw/agent/install/job_notification.hpp"
#include "lsw/agent/rpc/event_source.hpp"

namespace lsw::agent::install {
class Installer;
}

namespace lsw::agent::transport {

// Publishes install-job progress as ``setup_progress`` / ``setup_done``.
//
// The one source that implements ``undeliverable``: the wizard blocks on
// ``setup_done``, so a batch lost to a connection dying mid-drain — the
// classic case being guest-tools resetting the NetKVM NIC this very
// channel rides on — must be redelivered rather than dropped. The typed
// notifications are kept alongside the events precisely so they can go
// back on the installer's queue unchanged.
class InstallerEventSource : public rpc::EventSource {
public:
    explicit InstallerEventSource(install::Installer& installer)
        : installer_(&installer) {}

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;
    void undeliverable(std::size_t first_undelivered) override;

private:
    install::Installer* installer_;
    std::vector<install::JobNotification> batch_;
};

}  // namespace lsw::agent::transport
