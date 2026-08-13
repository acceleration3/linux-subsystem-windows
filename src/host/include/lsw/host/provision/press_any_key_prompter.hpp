#pragma once

#include <QObject>
#include <QTimer>
#include <QUuid>

namespace lsw::host::vm { class LibvirtConnection; }

namespace lsw::host::provision {

// Taps the space bar at the freshly-booted VM for a few seconds.
//
// Windows install media boots to "Press any key to boot from CD or
// DVD..." and gives up after about five seconds. Nobody is watching the
// VNC console during an unattended install, so the manager presses it —
// a handful of synthetic key events through libvirt, then it stops.
//
// Fire-and-forget by design: there is no way to tell whether the prompt
// was there, and a key sent to an already-booting installer is harmless.
class PressAnyKeyPrompter : public QObject {
    Q_OBJECT
public:
    // ~4 seconds at the 250 ms tick, comfortably covering the prompt.
    static constexpr int kTicks = 16;
    static constexpr int kIntervalMs = 250;

    PressAnyKeyPrompter(vm::LibvirtConnection& libvirt, QObject* parent = nullptr);

    // Begin tapping at ``uuid``. Stops on its own after ``kTicks``.
    void start(const QUuid& uuid);
    void stop();

private Q_SLOTS:
    void on_tick();

private:
    vm::LibvirtConnection& libvirt_;
    QUuid uuid_;
    int ticks_remaining_ = 0;
    QTimer* timer_;
};

}  // namespace lsw::host::provision
