#pragma once

#include "lsw/host/manager/network_state.hpp"
#include "lsw/host/platform/host_addresses.hpp"
#include "lsw/host/platform/host_bridges.hpp"
#include "lsw/host/ui/scroll_page.hpp"

#include <functional>

class QFrame;
class QLabel;

namespace lsw::host::config {
class ManagerSettings;
class PendingChanges;
}  // namespace lsw::host::config

namespace lsw::host::services {
class StatusHub;
}

namespace lsw::host::ui {
class ChoiceCard;
}

namespace lsw::host::manager {

class BridgePicker;
class PortForwardEditor;
class SettingControls;

class NetworkPage : public ui::ScrollPage {
    Q_OBJECT
public:
    struct Host {
        std::function<platform::HostBridges::Facts()> bridges;
        std::function<QVector<platform::HostAddresses::Interface>()> interfaces;

        [[nodiscard]] static Host system();
    };

    NetworkPage(services::StatusHub& status, config::ManagerSettings& settings,
                config::PendingChanges& pending, Host host = Host::system(),
                QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;

    void read_host();

Q_SIGNALS:
    void summary_changed();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void build_connection();
    void build_adapter();
    void build_bridge();
    void build_forwards();
    void refresh();
    void refresh_now(const NetworkState::Live& live);
    void refresh_adapter(const NetworkState::Live& live);
    void choose_mode(const QString& mode);
    void order_tabs();
    [[nodiscard]] int waiting() const;

    services::StatusHub& status_;
    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
    Host host_;
    SettingControls* controls_ = nullptr;
    platform::HostBridges::Facts facts_;
    QVector<platform::HostAddresses::Interface> interfaces_;
    NetworkState::Live seen_;

    ui::ChoiceCard* shared_ = nullptr;
    ui::ChoiceCard* bridged_ = nullptr;
    QLabel* off_ = nullptr;
    QWidget* now_ = nullptr;
    QLabel* adapters_ = nullptr;
    QLabel* address_ = nullptr;
    QWidget* address_copy_ = nullptr;
    QLabel* address_missing_ = nullptr;
    QLabel* not_bridged_ = nullptr;

    ui::ChoiceCard* virtio_ = nullptr;
    ui::ChoiceCard* emulated_ = nullptr;
    QLabel* driver_note_ = nullptr;

    QFrame* bridge_area_ = nullptr;
    BridgePicker* picker_ = nullptr;

    PortForwardEditor* forwards_ = nullptr;
    QLabel* forward_count_ = nullptr;
};

}  // namespace lsw::host::manager
