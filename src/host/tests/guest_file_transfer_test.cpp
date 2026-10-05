#include "lsw/agent/apps/upload_store.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/host/apps/guest_file_transfer.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/linkproto/commands.hpp"
#include "lsw/linkproto/envelope.hpp"
#include "lsw/linkproto/file_transfer.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

namespace {
void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

bool wait_for(const std::function<bool()>& done) {
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    poll.start(5);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    if (!done())
        loop.exec();
    return done();
}

enum class Mode {
    Normal,
    RejectBegin,
    RejectChunk,
    BadOffset,
    RejectFinish,
    CancelBegin,
    CancelChunk,
    CancelFinish,
    DropChunk
};
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir local;
    QTemporaryDir guest;
    check(local.isValid() && guest.isValid(), "test directories available");
    lsw::agent::apps::UploadStore store(guest.path().toStdString());
    using lsw::host::apps::GuestFileTransfer;
    lsw::host::link::AgentRpcClient rpc;
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "fake agent listening");
    rpc.set_target(1, server.serverPort());
    Mode mode = Mode::Normal;
    GuestFileTransfer* current = nullptr;
    int requests = 0;
    int chunks = 0;
    int finishes = 0;
    int aborts = 0;
    std::string last_id;
    QByteArray received;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        auto* socket = server.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
            *buffer += socket->readAll();
            while (buffer->size() >= 5) {
                const auto size = lsw::linkproto::read_payload_len(
                    reinterpret_cast<const std::uint8_t*>(buffer->constData()));
                if (buffer->size() < 5 + size)
                    break;
                const auto message = nlohmann::json::parse(buffer->mid(5, size).toStdString());
                buffer->remove(0, 5 + size);
                ++requests;
                check(message["cmd"] == lsw::linkproto::cmd::kUploadFile,
                      "uses shared upload command");
                const auto& params = message["params"];
                const auto action = params["action"].get<std::string>();
                nlohmann::json body = {{"ok", true}};
                if (action == "begin") {
                    received.clear();
                    check(params["name"] == "setup space.msi", "basename not host path sent");
                    if (mode == Mode::RejectBegin) {
                        body = {{"ok", false}, {"error", "agent too old: unknown command"}};
                    } else {
                        last_id = store.begin(params["name"].get<std::string>(),
                                              params["size"].get<std::uint64_t>());
                        body["upload_id"] = last_id;
                        if (mode == Mode::CancelBegin)
                            current->cancel();
                    }
                } else if (action == "chunk") {
                    ++chunks;
                    if (mode == Mode::DropChunk) {
                        socket->abort();
                        return;
                    }
                    if (mode == Mode::RejectChunk) {
                        body = {{"ok", false}, {"error", "disk full"}};
                    } else {
                        const auto bytes =
                            lsw::agent::util::Base64::decode(params["data"].get<std::string>());
                        check(bytes.size() <= lsw::linkproto::kFileChunkBytes, "chunk is bounded");
                        check(params["offset"].get<std::uint64_t>() ==
                                  static_cast<std::uint64_t>(received.size()),
                              "chunks are sequential");
                        received.append(reinterpret_cast<const char*>(bytes.data()),
                                        static_cast<qsizetype>(bytes.size()));
                        body["offset"] = store.append(params["upload_id"].get<std::string>(),
                                                      params["offset"].get<std::uint64_t>(), bytes);
                        if (mode == Mode::BadOffset)
                            body["offset"] = 7;
                        if (mode == Mode::CancelChunk)
                            current->cancel();
                    }
                } else if (action == "finish") {
                    ++finishes;
                    if (mode == Mode::RejectFinish) {
                        body = {{"ok", false}, {"error", "flush failed"}};
                    } else {
                        body["path"] =
                            store.finish(params["upload_id"].get<std::string>()).string();
                        if (mode == Mode::CancelFinish)
                            current->cancel();
                    }
                } else if (action == "abort") {
                    ++aborts;
                    store.abort(params["upload_id"].get<std::string>());
                } else {
                    check(false, "known action");
                }
                const auto reply = nlohmann::json{{"id", message["id"]}, {"result", body}}.dump();
                std::uint8_t header[5];
                lsw::linkproto::write_header(header, lsw::linkproto::FrameType::Control,
                                             static_cast<std::uint32_t>(reply.size()));
                socket->write(reinterpret_cast<const char*>(header), 5);
                socket->write(reply.data(), static_cast<qint64>(reply.size()));
            }
        });
    });

    QByteArray payload(static_cast<qsizetype>(lsw::linkproto::kFileChunkBytes * 2 + 17), '\0');
    for (qsizetype i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>(i % 256);
    const QString path = local.filePath(QStringLiteral("setup space.msi"));
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(payload) == payload.size(),
          "binary source written");
    file.close();

    QString completed_path;
    for (const auto test_mode :
         {Mode::Normal, Mode::RejectBegin, Mode::RejectChunk, Mode::BadOffset, Mode::RejectFinish,
          Mode::CancelBegin, Mode::CancelChunk, Mode::CancelFinish, Mode::DropChunk}) {
        mode = test_mode;
        chunks = finishes = 0;
        const int before_aborts = aborts;
        GuestFileTransfer transfer(rpc);
        current = &transfer;
        QString destination;
        QString error;
        bool cancelled = false;
        qint64 acknowledged = -1;
        QObject::connect(&transfer, &GuestFileTransfer::transferred, &app,
                         [&](const QString& guest_path) { destination = guest_path; });
        QObject::connect(&transfer, &GuestFileTransfer::failed, &app,
                         [&](const QString& why) { error = why; });
        QObject::connect(&transfer, &GuestFileTransfer::cancelled, &app, [&] { cancelled = true; });
        QObject::connect(&transfer, &GuestFileTransfer::progress, &app,
                         [&](qint64 sent, qint64 total) {
                             check(sent >= acknowledged && sent <= total && total == payload.size(),
                                   "progress counts acknowledged bytes");
                             acknowledged = sent;
                         });
        transfer.start(path);
        transfer.start(path);  // Ignored while busy.
        check(wait_for([&] { return !transfer.busy(); }),
              "transfer completes without blocking GUI");
        if (mode == Mode::Normal) {
            check(!destination.isEmpty() && error.isEmpty() && !cancelled,
                  "transfer returns destination only on success");
            check(chunks == 3 && finishes == 1 && acknowledged == payload.size(),
                  "multi-chunk upload finishes once");
            QFile copy(destination);
            check(copy.open(QIODevice::ReadOnly) && copy.readAll() == payload &&
                      received == payload,
                  "end-to-end transfer preserves every binary byte");
            completed_path = destination;
        } else if (mode == Mode::RejectBegin) {
            check(!error.isEmpty() && chunks == 0 && finishes == 0,
                  "unsupported agent reports failure before transfer");
        } else {
            check(destination.isEmpty(),
                  "failed or cancelled transfer never publishes a path to launch");
            check(wait_for([&] { return aborts > before_aborts; }), "partial transfer aborted");
            check(mode == Mode::CancelFinish ||
                      !QFile::exists(
                          guest.filePath(QString::fromStdString(last_id) + "/upload.part")),
                  "partial file removed on error and cancellation");
            if (mode == Mode::CancelBegin || mode == Mode::CancelChunk ||
                mode == Mode::CancelFinish)
                check(cancelled && error.isEmpty(), "cancel is not a transfer error");
            else
                check(!error.isEmpty(), "transport and guest errors propagated");
            if (mode == Mode::CancelBegin)
                check(chunks == 0 && finishes == 0, "late begin reply is aborted before copying");
            if (mode == Mode::CancelChunk || mode == Mode::RejectChunk || mode == Mode::BadOffset ||
                mode == Mode::DropChunk)
                check(finishes == 0, "failed chunk cannot commit upload");
        }
        check(QFile::exists(completed_path), "cleanup leaves previously completed upload intact");
    }
    mode = Mode::Normal;
    GuestFileTransfer invalid(rpc);
    int local_errors = 0;
    QObject::connect(&invalid, &GuestFileTransfer::failed, &app,
                     [&](const QString&) { ++local_errors; });
    const int before_requests = requests;
    invalid.start(local.filePath(QStringLiteral("missing.exe")));
    QFile empty(local.filePath(QStringLiteral("empty.exe")));
    check(empty.open(QIODevice::WriteOnly), "empty fixture created");
    empty.close();
    invalid.start(empty.fileName());
    invalid.start(local.path());
    check(local_errors == 3 && requests == before_requests,
          "invalid local files fail without contacting guest");
    rpc.clear_target();
    invalid.start(path);
    check(local_errors == 4 && requests == before_requests, "no-target error stays local");
    QThreadPool::globalInstance()->waitForDone();
    std::puts("guest file transfer tests passed");
}
