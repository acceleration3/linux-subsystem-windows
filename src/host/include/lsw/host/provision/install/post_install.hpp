#pragma once

#include "lsw/host/platform/vsock_client.hpp"

#include <QObject>
#include <QTimer>
#include <QUuid>

namespace lsw::host::provision::install {
class PostInstallWatcher : public QObject {
    Q_OBJECT
public:
    PostInstallWatcher(QUuid vm_uuid, quint32 cid, quint32 agent_port, QObject* parent = nullptr);

    void start();
    void stop();

Q_SIGNALS:
    void ready(const QUuid& vm_uuid);

private Q_SLOTS:
    void on_tick();

private:
    QUuid uuid_;
    quint32 cid_;
    quint32 port_;
    QTimer timer_;
    bool probe_in_flight_ = false;
};

}
