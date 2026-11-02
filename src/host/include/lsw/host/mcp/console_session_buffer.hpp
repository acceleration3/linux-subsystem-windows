#pragma once

#include "lsw/host/link/event_subscription.hpp"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <cstddef>
#include <mutex>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {
class ConsoleSessionBuffer {
public:
    static constexpr std::size_t kCapacity = 256 * 1024;

    explicit ConsoleSessionBuffer(link::AgentRpcClient& rpc);

    ConsoleSessionBuffer(const ConsoleSessionBuffer&) = delete;
    ConsoleSessionBuffer& operator=(const ConsoleSessionBuffer&) = delete;

    [[nodiscard]] QByteArray take(const QString& session_id);

    void forget(const QString& session_id);

private:
    void on_output(const QString& session_id, const QByteArray& bytes);

    mutable std::mutex mu_;
    QHash<QString, QByteArray> sessions_;

    link::EventSubscription output_sub_;
    link::EventSubscription closed_sub_;
};

}
