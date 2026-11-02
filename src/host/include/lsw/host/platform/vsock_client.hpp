#pragma once

#include <QByteArray>
#include <QObject>

namespace lsw::host::platform {
class VsockClient : public QObject {
    Q_OBJECT
public:
    explicit VsockClient(QObject* parent = nullptr);
    ~VsockClient() override;

    [[nodiscard]] int connect(quint32 cid, quint32 port);
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    [[nodiscard]] int send_all(const QByteArray& data);

    [[nodiscard]] QByteArray recv_some(int n);

private:
    int fd_ = -1;
};

}
