#pragma once

#include "lsw/host/link/event_bus.hpp"
#include "lsw/host/link/event_subscription.hpp"
#include "lsw/host/platform/vsock_client.hpp"

#include <QByteArray>
#include <QObject>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::link {

struct AgentReply;
class PendingCalls;

class AgentRpcClient : public QObject {
    Q_OBJECT
public:
    using EventHandler = EventBus::Handler;

    explicit AgentRpcClient(QObject* parent = nullptr);
    ~AgentRpcClient() override;

    void set_target(quint32 cid, quint32 port);
    void clear_target();

    [[nodiscard]] bool has_target() const noexcept {
        return cid_.load() != 0 && port_.load() != 0;
    }
    [[nodiscard]] quint32 port() const noexcept { return port_.load(); }

    static constexpr std::chrono::milliseconds kDefaultCallTimeout{10000};

    [[nodiscard]] AgentReply request(std::string_view cmd);
    [[nodiscard]] AgentReply request(std::string_view cmd, const nlohmann::json& params,
                                     std::chrono::milliseconds timeout = kDefaultCallTimeout);

    void request_async(std::string_view cmd, nlohmann::json params, QObject* context,
                       std::function<void(const AgentReply&)> done);

    void request_detached(std::string_view cmd, nlohmann::json params);

    [[nodiscard]] EventSubscription subscribe(std::string_view topic, EventHandler handler);

private:
    void reader_loop();

    bool ensure_connected_locked();
    bool send_locked(const QByteArray& body);
    void drop_connection_locked();

    void route_message(const nlohmann::json& message);

    void handle_control(const std::uint8_t* payload, std::size_t len);

    mutable std::mutex io_mu_;
    platform::VsockClient client_;

    std::atomic<quint64> next_id_{1};

    std::unique_ptr<PendingCalls> pending_;
    std::unique_ptr<EventBus> events_;

    std::atomic<quint32> cid_{0};
    std::atomic<quint32> port_{0};
    std::atomic<quint64> generation_{0};
    quint64 last_generation_ = 0;
    std::atomic<bool> announced_{false};

    std::atomic<bool> stop_{false};
    std::thread reader_;
};

}
