#pragma once

#include <QDateTime>
#include <QUuid>

namespace lsw::host::vm {

// Follows the av-renderer process of a VM's QEMU.
//
// With the host GPU stack isolated, accel-virt runs it in av-renderer,
// a child QEMU starts and restarts. A crash inside a host driver kills
// it: the guest sees a device reset, which Windows recovers from, and
// QEMU starts a new one. Nothing reports that except the pid changing,
// so this reads /proc: the QEMU whose command line carries the VM's
// ``-uuid``, and its child named av-renderer.
//
// Split in two so the reading can run on a worker: ``scan`` touches
// only /proc, ``update`` only this object's state.
class RendererWatcher {
public:
    // What /proc says now. 0 is "not found".
    struct Sample {
        qint64 qemu_pid = 0;
        qint64 renderer_pid = 0;
    };

    // Read /proc for ``uuid``'s QEMU — ``known_qemu`` first, if it is
    // still there, which saves reading every command line — and its
    // renderer. Safe on any thread.
    [[nodiscard]] static Sample scan(const QUuid& uuid, qint64 known_qemu);

    // Take a sample. True when it shows a renderer that replaced
    // another under the same QEMU: the host graphics driver crashed.
    bool update(const Sample& sample);

    // Start over: another VM, or none.
    void reset();

    // 0 when not found: the VM is off, isolation is off, or QEMU is
    // between renderers.
    [[nodiscard]] qint64 qemu_pid() const noexcept { return qemu_pid_; }
    [[nodiscard]] qint64 renderer_pid() const noexcept { return renderer_pid_; }
    // Renderers that replaced another under this QEMU.
    [[nodiscard]] int restarts() const noexcept { return restarts_; }
    [[nodiscard]] QDateTime last_restart() const { return last_restart_; }

private:
    qint64 qemu_pid_ = 0;
    qint64 renderer_pid_ = 0;
    // The last renderer seen under this QEMU, kept across a gap in which
    // there was none, so the one after it counts as a restart.
    qint64 previous_renderer_ = 0;
    int restarts_ = 0;
    QDateTime last_restart_;
};

}  // namespace lsw::host::vm
