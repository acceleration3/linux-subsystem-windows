#pragma once

#include "lsw/agent/install/job_notification.hpp"
#include "lsw/agent/rpc/event_source.hpp"

namespace lsw::agent::install {
class Installer;
}

namespace lsw::agent::transport {
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

}
