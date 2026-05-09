#pragma once

#include "lsw/manager/vm/vsock.hpp"

#include <QObject>
#include <QTimer>
#include <QUuid>

namespace lsw::manager::install {

// Polls the guest agent's vsock service GUID until it answers. The
// wizard's install page binds to the ``ready`` signal to advance.
//
// One PostInstallWatcher per VM; fires ``ready`` exactly once and then
// stops the timer.
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
    // The vsock connect probe runs on a worker thread (kernel connect()
    // can block several seconds against an unreachable guest). This
    // flag prevents stacking up multiple in-flight probes if a probe
    // outlasts the next timer tick.
    bool probe_in_flight_ = false;
};

}  // namespace lsw::manager::install
