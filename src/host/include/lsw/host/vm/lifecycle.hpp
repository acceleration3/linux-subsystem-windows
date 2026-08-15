#pragma once

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/vm/domain_xml_builder.hpp"
#include "lsw/host/vm/ivshmem_server.hpp"
#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/shmem_region.hpp"

#include <QObject>
#include <QUuid>

#include <memory>
#include <unordered_map>

namespace lsw::host::vm {

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
    // ``first_boot_done`` says the guest has run before, which is what
    // makes it safe — and right — to drop any install media still
    // attached before starting. See the note in the implementation.
    [[nodiscard]] int start(const QUuid& uuid, bool first_boot_done = false);

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

    // Live ShmemRegion / IvshmemServer for ``uuid`` (the WGC capture
    // transport). Non-null only while the VM is running. The manager's
    // ``AgentRpcClient`` uses these to receive frame doorbells and
    // decode them out of the shared region. Returns null otherwise.
    [[nodiscard]] ShmemRegion* shmem_region(const QUuid& uuid) const;
    [[nodiscard]] IvshmemServer* ivshmem_server(const QUuid& uuid) const;

Q_SIGNALS:
    // The bundle for ``uuid`` just came up / went away. Consumers bind
    // here rather than at their own construction time: the bundle only
    // exists between VM start and force-off, which is a window neither
    // the UI nor the launch path is inside when it first runs. Binding
    // on a timer of one's own choosing means binding to null, silently,
    // and never being asked again — which is exactly how the frame
    // doorbells ended up connected to nothing.
    void shmem_ready(const QUuid& uuid);
    void shmem_gone(const QUuid& uuid);

private:
    struct ShmemBundle {
        std::unique_ptr<ShmemRegion> region;
        std::unique_ptr<IvshmemServer> server;
    };
    [[nodiscard]] DomainHandle lookup(const QUuid& uuid) const;
    // Create + bind the per-VM region and server. Returns the socket
    // path (substituted into domain XML) or empty on failure.
    [[nodiscard]] QString ensure_shmem(const QUuid& uuid, int size_mib);
    void teardown_shmem(const QUuid& uuid);

    struct QUuidHash {
        std::size_t operator()(const QUuid& u) const noexcept {
            return qHash(u);
        }
    };
    LibvirtConnection& conn_;
    std::unordered_map<QUuid, ShmemBundle, QUuidHash> shmem_;
};

}  // namespace lsw::host::vm
