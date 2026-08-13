#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <cstddef>
#include <mutex>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::mcp {

// Accumulates cmd.exe output pushed by the agent so the model can poll
// for it later.
//
// The agent pushes ``console_output`` events as bytes arrive, but MCP is
// request/response: nobody is listening at the moment the bytes land.
// This is the gap-filler. Bounded, because a chatty cmd.exe would
// otherwise grow the manager's heap without limit.
//
// Thread-safe: filled from the channel's reader thread, drained from
// HTTP worker threads.
class ConsoleSessionBuffer {
public:
    // Per-session cap. Oldest bytes are dropped first.
    static constexpr std::size_t kCapacity = 256 * 1024;

    explicit ConsoleSessionBuffer(link::AgentRpcClient& rpc);
    ~ConsoleSessionBuffer();

    ConsoleSessionBuffer(const ConsoleSessionBuffer&) = delete;
    ConsoleSessionBuffer& operator=(const ConsoleSessionBuffer&) = delete;

    // Return everything buffered for ``session_id`` and clear it. Empty
    // when the session is unknown or nothing has arrived yet.
    [[nodiscard]] QByteArray take(const QString& session_id);

    // Drop a closed session's buffer.
    void forget(const QString& session_id);

private:
    void on_output(const QString& session_id, const QByteArray& bytes);

    link::AgentRpcClient& rpc_;
    mutable std::mutex mu_;
    QHash<QString, QByteArray> sessions_;

    // Retained so the destructor unregisters cleanly — the reader thread
    // must not call back into a destroyed buffer.
    unsigned long long output_token_ = 0;
    unsigned long long closed_token_ = 0;
};

}  // namespace lsw::host::mcp
