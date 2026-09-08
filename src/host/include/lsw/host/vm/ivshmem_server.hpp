#pragma once

#include "lsw/linkproto/region.hpp"

#include <QObject>
#include <QString>
#include <QUuid>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

namespace lsw::host::vm {

class ShmemRegion;

// In-process implementation of QEMU's ivshmem-doorbell server
// protocol. QEMU's ``<shmem><server path=...></shmem>`` device
// connects to a Unix domain socket and expects a small fd-passing
// handshake:
//
//   1) server -> qemu : i64 protocol_version, no fd
//   2) server -> qemu : i64 -1,               SCM_RIGHTS shm_fd
//   3) server -> qemu : i64 qemu_peer_id,     no fd
//   For each vector V of every existing peer P (including us as
//   peer 0):
//     4) server -> qemu : i64 P,              SCM_RIGHTS P.eventfd[V]
//
// We act as the server AND as peer 0; QEMU is peer 1. Each of our
// ``kVectorCount`` eventfds is one doorbell;
// when the guest writes to the IVSHMEM outbound MMIO register
// addressing peer 0 vector V, QEMU writes a u64 1 to ``eventfds_[V]``
// and we emit ``channel_ready`` from a ``QSocketNotifier``.
class IvshmemServer : public QObject {
    Q_OBJECT
public:
    // One vector per frame slot, plus one per message channel. The
    // split is the shared contract's (linkproto::kRpcDoorbellVector and
    // friends), so a ring notification can never be mistaken for a
    // frame. Must match ``<msi vectors='N'/>`` in domain.xml.
    static constexpr int kVectorCount = linkproto::kDoorbellVectorCount;
    // Our peer ID. QEMU is assigned 1.
    static constexpr std::int64_t kSelfPeerId = 0;
    static constexpr std::int64_t kQemuPeerId = 1;
    // The version QEMU expects to negotiate. 0 = current upstream
    // ivshmem protocol.
    static constexpr std::int64_t kProtocolVersion = 0;

    IvshmemServer(const QUuid& vm_uuid, ShmemRegion& region,
                  QObject* parent = nullptr);
    ~IvshmemServer() override;

    // Bind ``/tmp/lsw-ivshmem-<uuid>.sock`` and start listening. Must
    // be called BEFORE ``virDomainCreate`` — libvirt fails the start
    // if the chardev path doesn't accept. Returns 0 / -1 (logged).
    [[nodiscard]] int start();

    // Tear down: close client + listen sockets, close eventfds,
    // unlink the socket file. Idempotent.
    void stop();

    // The path libvirt's ``<shmem><server path=.../></shmem>``
    // chardev points at. Stable for the VM's lifetime.
    [[nodiscard]] QString socket_path() const { return socket_path_; }

    // The path a server for ``uuid`` will bind. Derived rather than
    // stored, so a domain can be defined with the right chardev before
    // any server exists to bind it.
    [[nodiscard]] static QString socket_path_for(const QUuid& uuid);

Q_SIGNALS:
    // Fired on the GUI thread whenever the guest doorbells our peer
    // (id 0) on a frame vector. ``slot_index`` is identical to the
    // ShmemRegion slot the doorbell refers to.

    // Fired when the guest doorbells one of the message channels:
    // ``kind`` is a ``linkproto::kChannel*`` value and there is at
    // least one message waiting in that channel's guest→host ring.
    void channel_ready(unsigned kind);

private:
    // All socket I/O runs on ``worker_`` so that ``virDomainCreate``
    // (which blocks the GUI thread waiting for QEMU to finish init)
    // doesn't deadlock against QEMU's own wait for our handshake
    // reply on the same chardev socket. The thread does its own
    // ``poll()`` over the listen fd, the accepted peer fd, the
    // ``stop_eventfd_``, and every doorbell ``eventfd``. Frame-ready
    // signals are emitted via ``QMetaObject::invokeMethod`` with
    // ``Qt::QueuedConnection`` so consumers stay on their own thread.
    void worker_loop();
    bool send_msg(int peer_fd, std::int64_t value, int fd);
    void close_peer();

    QUuid   uuid_;
    ShmemRegion& region_;
    QString socket_path_;
    int     listen_fd_   = -1;
    int     stop_eventfd_ = -1;
    std::array<int, kVectorCount> eventfds_{};
    std::thread       worker_;
    std::atomic<bool> stop_{false};
};

}  // namespace lsw::host::vm
