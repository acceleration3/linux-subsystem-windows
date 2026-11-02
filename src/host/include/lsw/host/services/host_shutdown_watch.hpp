#pragma once

#include <QDBusUnixFileDescriptor>
#include <QObject>

namespace lsw::host::vm {
class Lifecycle;
}

namespace lsw::host::services {

class StatusHub;

// Powers the guest off the moment the host announces a shutdown or a reboot.
//
// libvirt's session daemon handles a host shutdown by itself, and slowly: it
// tries a managed save (which the GPU device's migration blocker refuses),
// then asks Windows to shut down and waits, until logind's delay runs out and
// systemd kills QEMU part way through Windows' own shutdown, holding the host
// back the whole time. Here logind's PrepareForShutdown is the cue: the domain
// is destroyed at once, and only then is the delay lock let go.
class HostShutdownWatch : public QObject {
    Q_OBJECT
public:
    HostShutdownWatch(vm::Lifecycle& lifecycle, StatusHub& status, QObject* parent = nullptr);

    // Takes logind's delay lock and listens for the announcement. Called by
    // the process that stays, never by one that hands its request on and
    // exits. Idempotent.
    void start();

private Q_SLOTS:
    void prepare_for_shutdown(bool starting);

private:
    void take_lock();

    vm::Lifecycle& lifecycle_;
    StatusHub& status_;
    // Held while a shutdown would wait for it; closed to let one go on.
    QDBusUnixFileDescriptor lock_;
    bool started_ = false;
};

}  // namespace lsw::host::services
