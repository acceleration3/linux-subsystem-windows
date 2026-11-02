#include "lsw/host/link/agent_guest_files.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QFile>

#include <chrono>
#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::link {

namespace {

constexpr std::chrono::milliseconds kPackTimeout{150000};
constexpr std::chrono::milliseconds kClearTimeout{60000};

template <typename Sink>
bool read_chunks(AgentRpcClient& rpc, const std::string& path, std::string& error,
                 bool delete_when_done, Sink&& sink) {
    std::uint64_t offset = 0;
    for (;;) {
        const auto reply = rpc.request(
            linkproto::cmd::kReadFile,
            {{"path", path},
             {"offset", offset},
             {"length", AgentGuestFiles::kChunkBytes},
             {"delete_at_eof", delete_when_done}});
        if (!reply.ok) {
            error = reply.error;
            return false;
        }
        const QByteArray data = QByteArray::fromBase64(
            QByteArray::fromStdString(reply.body.value("data", std::string{})));
        if (!sink(data)) {
            error = "cannot write the local file";
            return false;
        }
        offset += static_cast<std::uint64_t>(data.size());
        if (reply.body.value("eof", true)) {
            return true;
        }
        if (data.isEmpty()) {
            error = "read_file returned no data before the end of " + path;
            return false;
        }
    }
}

}

bool AgentGuestFiles::read(const std::string& guest_path, QByteArray& out, std::string& error,
                           bool delete_when_done) {
    out.clear();
    return read_chunks(rpc_, guest_path, error, delete_when_done, [&out](const QByteArray& data) {
        out.append(data);
        return true;
    });
}

bool AgentGuestFiles::read_to_file(const std::string& guest_path, const QString& local_path,
                                   std::string& error, bool delete_when_done) {
    QFile file{local_path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "cannot write " + local_path.toStdString() + ": " + file.errorString().toStdString();
        return false;
    }
    const bool ok = read_chunks(rpc_, guest_path, error, delete_when_done,
                                [&file](const QByteArray& data) { return file.write(data) == data.size(); });
    file.close();
    if (!ok) {
        QFile::remove(local_path);
    }
    return ok;
}

AgentGuestFiles::Packed AgentGuestFiles::pack_logs() {
    const auto reply = rpc_.request(linkproto::cmd::kPackLogs, nlohmann::json::object(), kPackTimeout);
    Packed packed;
    if (!reply.ok) {
        packed.error = reply.error;
        return packed;
    }
    packed.ok = true;
    packed.path = reply.body.value("path", std::string{});
    packed.size = reply.body.value("size", std::uint64_t{0});
    return packed;
}

AgentGuestFiles::Cleared AgentGuestFiles::clear_logs() {
    const auto reply = rpc_.request(linkproto::cmd::kClearLogs, nlohmann::json::object(), kClearTimeout);
    Cleared cleared;
    if (!reply.ok) {
        cleared.error = reply.error;
        return cleared;
    }
    cleared.ok = true;
    cleared.deleted = reply.body.value("deleted", 0);
    cleared.emptied = reply.body.value("emptied", 0);
    if (const auto it = reply.body.find("skipped"); it != reply.body.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_string()) {
                cleared.skipped.push_back(entry.get<std::string>());
            }
        }
    }
    return cleared;
}

}
