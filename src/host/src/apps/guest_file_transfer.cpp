#include "lsw/host/apps/guest_file_transfer.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/file_transfer.hpp"

#include <QFileInfo>
#include <nlohmann/json.hpp>

namespace lsw::host::apps {

GuestFileTransfer::GuestFileTransfer(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

GuestFileTransfer::~GuestFileTransfer() {
    abort();
}

void GuestFileTransfer::abort() {
    if (!upload_id_.isEmpty() && rpc_.port() == port_) {
        rpc_.request_detached(linkproto::cmd::kUploadFile,
                              {{"action", "abort"}, {"upload_id", upload_id_.toStdString()}});
    }
    upload_id_.clear();
    file_.close();
}

void GuestFileTransfer::fail(const QString& error) {
    abort();
    busy_ = false;
    Q_EMIT failed(error);
}

bool GuestFileTransfer::stop_if_cancelled() {
    if (rpc_.port() != port_) {
        fail(QStringLiteral("The target VM changed during the transfer"));
        return true;
    }
    if (!cancel_requested_)
        return false;
    abort();
    busy_ = false;
    Q_EMIT cancelled();
    return true;
}

void GuestFileTransfer::cancel() {
    // The outstanding callback observes this before sending another chunk
    // or publishing success. In particular, a late begin reply is aborted.
    cancel_requested_ = true;
}

void GuestFileTransfer::start(const QString& local_path) {
    if (busy_)
        return;
    cancel_requested_ = false;
    offset_ = 0;
    port_ = rpc_.port();
    if (!rpc_.has_target()) {
        fail(QStringLiteral("No Windows VM is connected"));
        return;
    }
    const QFileInfo info(local_path);
    file_.setFileName(local_path);
    if (!info.isFile() || !file_.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("Cannot read the selected file: %1").arg(file_.errorString()));
        return;
    }
    size_ = file_.size();
    if (size_ <= 0 || static_cast<std::uint64_t>(size_) > linkproto::kMaxUploadBytes) {
        fail(QStringLiteral("Choose a non-empty file no larger than 16 GiB"));
        return;
    }
    busy_ = true;
    Q_EMIT progress(0, size_);
    rpc_.request_async(linkproto::cmd::kUploadFile,
                       {{"action", "begin"},
                        {"name", info.fileName().toStdString()},
                        {"size", static_cast<std::uint64_t>(size_)}},
                       this, [this](const link::AgentReply& reply) {
                           if (reply.ok)
                               upload_id_ = QString::fromStdString(
                                   reply.body.value("upload_id", std::string{}));
                           if (stop_if_cancelled())
                               return;
                           if (!reply.ok || upload_id_.isEmpty()) {
                               fail(reply.ok ? QStringLiteral("The agent returned no upload ID")
                                             : QString::fromStdString(reply.error));
                               return;
                           }
                           next_chunk();
                       });
}

void GuestFileTransfer::next_chunk() {
    if (stop_if_cancelled())
        return;
    if (offset_ == size_) {
        if (file_.size() != size_) {
            fail(QStringLiteral("The selected file changed size during the transfer"));
            return;
        }
        file_.close();
        rpc_.request_async(linkproto::cmd::kUploadFile,
                           {{"action", "finish"}, {"upload_id", upload_id_.toStdString()}}, this,
                           [this](const link::AgentReply& reply) {
                               if (stop_if_cancelled())
                                   return;
                               if (!reply.ok) {
                                   fail(QString::fromStdString(reply.error));
                                   return;
                               }
                               const auto path =
                                   QString::fromStdString(reply.body.value("path", std::string{}));
                               if (path.isEmpty()) {
                                   fail(QStringLiteral("The agent returned no destination path"));
                                   return;
                               }
                               upload_id_.clear();
                               busy_ = false;
                               Q_EMIT transferred(path);
                           });
        return;
    }
    const auto bytes = file_.read(static_cast<qint64>(linkproto::kFileChunkBytes));
    if (bytes.isEmpty() || file_.error() != QFileDevice::NoError ||
        bytes.size() > size_ - offset_) {
        fail(QStringLiteral("Cannot read the file, or it changed during the transfer"));
        return;
    }
    const qint64 expected = offset_ + bytes.size();
    rpc_.request_async(
        linkproto::cmd::kUploadFile,
        {{"action", "chunk"},
         {"upload_id", upload_id_.toStdString()},
         {"offset", static_cast<std::uint64_t>(offset_)},
         {"data", bytes.toBase64().toStdString()}},
        this, [this, expected](const link::AgentReply& reply) {
            if (stop_if_cancelled())
                return;
            if (!reply.ok || reply.body.value("offset", std::uint64_t{0}) !=
                                 static_cast<std::uint64_t>(expected)) {
                fail(reply.ok ? QStringLiteral("The agent acknowledged the wrong file offset")
                              : QString::fromStdString(reply.error));
                return;
            }
            offset_ = expected;
            Q_EMIT progress(offset_, size_);
            next_chunk();
        });
}

}  // namespace lsw::host::apps
