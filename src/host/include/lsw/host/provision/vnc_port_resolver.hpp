#pragma once

#include <QObject>
#include <QTimer>
#include <QUuid>

namespace lsw::host::vm { class Lifecycle; }

namespace lsw::host::provision {

// Waits for libvirt to assign the domain a VNC port, then reports it.
//
// The port only exists once QEMU is actually up, and ``autoport='yes'``
// means the manager cannot know it in advance — so this polls briefly
// after start rather than reading it from the XML it submitted.
class VncPortResolver : public QObject {
    Q_OBJECT
public:
    static constexpr int kIntervalMs = 250;
    static constexpr int kMaxAttempts = 80;  // ~20 s

    VncPortResolver(vm::Lifecycle& lifecycle, QObject* parent = nullptr);

    // Poll for ``uuid``'s graphics port. Gives up silently (logged)
    // after ``kMaxAttempts`` — a missing console is not worth failing a
    // provisioning run over.
    void start(const QUuid& uuid);
    void stop();

Q_SIGNALS:
    void resolved(int port);

private Q_SLOTS:
    void on_tick();

private:
    vm::Lifecycle& lifecycle_;
    QUuid uuid_;
    int attempts_ = 0;
    QTimer* timer_;
};

}  // namespace lsw::host::provision
