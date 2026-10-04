#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUuid>

namespace lsw::host::vm {

class Lifecycle;

// Stop a running guest the way a person would, and make sure it stops.
//
// An ACPI shutdown is a request: Windows may be installing updates, a
// dialog may be holding logoff, or the request may simply be lost while
// the guest is still booting. So this asks, watches libvirt until the
// domain is off, asks again now and then, and at the deadline pulls the
// plug. ``finished`` fires once the domain is off, never before — which
// is what a caller that wants to start it again needs, because a start
// on a domain still running is a no-op.
//
// The same sequence the provisioning pipeline's restart into the
// steady-state machine runs; this is it as a reusable piece. Polls on
// the event loop, so nothing blocks the GUI thread.
class GracefulShutdown : public QObject {
    Q_OBJECT
public:
    static constexpr int kPollMs = 2000;
    static constexpr qint64 kDefaultDeadlineMs = 3 * 60 * 1000;
    // Ask again every this many polls: a request sent while Windows was
    // still starting is dropped, not queued.
    static constexpr int kRepressTicks = 15;

    GracefulShutdown(Lifecycle& lifecycle, QUuid uuid, qint64 deadline_ms = kDefaultDeadlineMs,
                     QObject* parent = nullptr);

    // Ask, and start watching. A domain already off finishes on the
    // next turn of the event loop.
    void start();

Q_SIGNALS:
    // The domain is off. ``forced`` when the deadline passed and it was
    // destroyed rather than shut down.
    void finished(bool forced);

private:
    void poll();

    Lifecycle& lifecycle_;
    QUuid uuid_;
    qint64 deadline_ms_;
    QTimer timer_;
    QElapsedTimer clock_;
    int ticks_ = 0;
    bool forced_ = false;
};

}  // namespace lsw::host::vm
