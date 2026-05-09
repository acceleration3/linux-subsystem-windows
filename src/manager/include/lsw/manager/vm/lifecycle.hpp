#pragma once

#include "lsw/manager/config/vm_state.hpp"
#include "lsw/manager/vm/domain_xml.hpp"
#include "lsw/manager/vm/libvirt_conn.hpp"

#include <QObject>
#include <QUuid>

namespace lsw::manager::vm {

// VM lifecycle controller. Wraps the libvirt define / start / shutdown
// / undefine sequence and the qcow2 disk creation that goes with it.
//
// All methods take a ``LibvirtConnection&``; the manager keeps one
// per-process and serializes calls through the UI thread (libvirt
// connections are thread-affine).
class Lifecycle : public QObject {
    Q_OBJECT
public:
    explicit Lifecycle(LibvirtConnection& conn, QObject* parent = nullptr);

    // Allocate a sparse qcow2 disk at ``path`` of ``size_gib``. Returns
    // 0 on success, -1 on failure (logged). Uses ``qemu-img`` for
    // simplicity — much smaller blob than wiring libvirt's storage API.
    [[nodiscard]] int create_disk(const QString& path, int size_gib);

    // Build XML, ``virDomainDefineXML``, ``virDomainCreate``. On
    // success the domain is running and persisted. Returns 0/-1.
    // Overwrites any existing definition for the same UUID — use
    // ``start`` instead when you don't want to clobber the live
    // shape (e.g. post-virtio-swap, where the manager's defaults
    // would otherwise revert it to pre-virtio on the next click).
    [[nodiscard]] int define_and_start(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    // ``virDomainCreate`` on the already-defined domain. Doesn't
    // touch the persistent XML — what's defined stays defined.
    // Returns 0/-1.
    [[nodiscard]] int start(const QUuid& uuid);

    // ACPI shutdown request (``virDomainShutdown``). The guest may
    // ignore — pair with a UI timeout that escalates to ``destroy``.
    [[nodiscard]] int shutdown(const QUuid& uuid);

    // Hard kill (``virDomainDestroy``).
    [[nodiscard]] int force_off(const QUuid& uuid);

    // Undefine (delete persistent config, NOT the disk).
    [[nodiscard]] int undefine(const QUuid& uuid);

    // Whether libvirt knows about this VM and it's currently running.
    [[nodiscard]] bool is_running(const QUuid& uuid) const;

    // The live VNC graphics port of the running domain (the
    // ``<graphics type='vnc' autoport='yes'>`` libvirt assigned), or
    // -1 if the domain isn't up / has none yet. Used by the manager's
    // live-preview surface.
    [[nodiscard]] int vnc_port(const QUuid& uuid) const;

    // Eject the install media (Windows ISO sdb, payload ISO sdc,
    // virtio-win ISO sdd) once install + setup are done. Only the
    // CD-ROM *media* is removed; the cdrom devices and — crucially —
    // the vda virtio-blk system disk are untouched, so this carries
    // none of the controller-swap INACCESSIBLE_BOOT_DEVICE risk.
    // Applied LIVE+CONFIG when running, CONFIG-only otherwise.
    // Best-effort: a missing/already-empty drive is not an error.
    [[nodiscard]] int eject_install_media(const QUuid& uuid);

private:
    [[nodiscard]] DomainHandle lookup(const QUuid& uuid) const;

    LibvirtConnection& conn_;
};

}  // namespace lsw::manager::vm
