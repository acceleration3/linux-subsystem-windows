#pragma once

#include <QByteArray>
#include <QObject>

namespace lsw::manager::vm {

// Tiny vsock client wrapper used to talk to the guest agent inside a
// VM. Connects to ``(cid, port)`` synchronously, then exposes blocking
// ``send`` / ``recv`` for short request/reply messages. The render
// server has its own (much fancier) framing — this is just the manager
// → guest-agent channel.
class VsockClient : public QObject {
    Q_OBJECT
public:
    explicit VsockClient(QObject* parent = nullptr);
    ~VsockClient() override;

    [[nodiscard]] int connect(quint32 cid, quint32 port);
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    // Send all of ``data``; -1 / -errno on failure.
    [[nodiscard]] int send_all(const QByteArray& data);

    // Read up to ``n`` bytes; returns the bytes (possibly empty). On
    // EOF or error returns an empty array; check ``is_open()`` after.
    [[nodiscard]] QByteArray recv_some(int n);

private:
    int fd_ = -1;
};

}  // namespace lsw::manager::vm
