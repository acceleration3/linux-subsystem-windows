#include "lsw/host/mcp/console_session_buffer.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::mcp {

ConsoleSessionBuffer::ConsoleSessionBuffer(link::AgentRpcClient& rpc) {
    output_sub_ = rpc.subscribe(linkproto::topic::kConsoleOutput, [this](const nlohmann::json& f) {
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        if (sid.isEmpty()) {
            return;
        }
        const auto b64 = QByteArray::fromStdString(f.value("data_b64", std::string{}));
        on_output(sid, QByteArray::fromBase64(b64));
    });
    // A closed session gets a synthetic tail line rather than silence, so
    // a model polling console_read can tell "nothing yet" from "it ended".
    closed_sub_ = rpc.subscribe(linkproto::topic::kConsoleClosed, [this](const nlohmann::json& f) {
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        if (sid.isEmpty()) {
            return;
        }
        const auto code = f.value("exit_code", 0);
        on_output(sid, QStringLiteral("\n[session exited, code=%1]\n").arg(code).toUtf8());
    });
}

void ConsoleSessionBuffer::on_output(const QString& session_id, const QByteArray& bytes) {
    const std::lock_guard<std::mutex> lock(mu_);
    auto& buf = sessions_[session_id];
    buf.append(bytes);
    if (buf.size() > static_cast<qsizetype>(kCapacity)) {
        buf.remove(0, buf.size() - static_cast<qsizetype>(kCapacity));
    }
}

QByteArray ConsoleSessionBuffer::take(const QString& session_id) {
    const std::lock_guard<std::mutex> lock(mu_);
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return {};
    }
    QByteArray out = it.value();
    it.value().clear();
    return out;
}

void ConsoleSessionBuffer::forget(const QString& session_id) {
    const std::lock_guard<std::mutex> lock(mu_);
    sessions_.remove(session_id);
}

}  // namespace lsw::host::mcp
