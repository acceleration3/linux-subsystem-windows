#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/host_clipboard.hpp"

#include <QObject>
#include <memory>
#include <optional>

namespace lsw::host::config {
class ManagerSettings;
}

namespace lsw::host::services {

class StatusHub;

class ClipboardSync : public QObject {
    Q_OBJECT
public:
    ClipboardSync(link::AgentRpcClient& rpc, StatusHub& status, config::ManagerSettings& settings,
                  QObject* parent = nullptr);
    ~ClipboardSync() override;

    void start();

private:
    struct Push {
        std::optional<platform::ClipboardPayload> payload;
    };

    void apply_setting();
    void create_backend();
    void use_qt_backend();
    void wire(platform::HostClipboard& backend);
    void stop();
    void publish_status();
    void on_status();
    void on_guest_changed(bool text, bool image, std::optional<quint32> session);
    void pump_guest();
    void on_guest_content(const nlohmann::json& body);
    void on_host_changed(const platform::ClipboardPayload& payload);
    void on_host_cleared();
    void queue_push(Push push);
    void retry_guest(quint64 gen);
    void pump_push();

    link::AgentRpcClient& rpc_;
    StatusHub& status_;
    config::ManagerSettings& settings_;
    link::EventSubscription sub_;
    std::unique_ptr<platform::HostClipboard> clipboard_;
    platform::ClipboardPayload host_current_;
    std::optional<Push> pending_push_;
    bool push_in_flight_ = false;
    quint64 guest_gen_ = 0;
    bool guest_dirty_ = false;
    bool guest_in_flight_ = false;
    quint64 guest_retried_ = 0;
    std::optional<quint32> guest_session_;
    bool host_undelivered_ = false;
    bool started_ = false;
    bool enabled_ = false;
    bool online_ = false;
};

}
