#pragma once

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/domain_xml_builder.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QUuid>
#include <optional>

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

    // Grow the image at ``path`` to ``size_gib``, and only grow it: a
    // target at or below what the image already is changes nothing and
    // succeeds, because shrinking a disk under a filesystem destroys it.
    // The guest must be off — QEMU holds the image locked while it runs
    // — and Windows extends C: into the new space at its next start (the
    // agent does it). False with ``error_out`` set when qemu-img refuses.
    [[nodiscard]] static bool grow_disk(const QString& path, int size_gib, QString* error_out);

    // Build XML, ``virDomainDefineXML``, ``virDomainCreate``. On
    // success the domain is running and persisted. Returns 0/-1.
    // Overwrites any existing definition for the same UUID — use
    // ``start`` instead when you don't want to clobber the live
    // shape (e.g. post-virtio-swap, where the manager's defaults
    // would otherwise revert it to pre-virtio on the next click).
    [[nodiscard]] int define_and_start(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    // Why the last define or start failed, in libvirt's words — which for
    // a QEMU that would not start carry QEMU's own reason. What a person
    // is shown instead of "see the logs".
    [[nodiscard]] const QString& last_error() const noexcept { return last_error_; }

    // ``virDomainCreate`` on the already-defined domain. Doesn't
    // touch the persistent XML — what's defined stays defined.
    // Returns 0/-1.
    // ``first_boot_done`` says the guest has run before, which is what
    // makes it safe — and right — to drop any install media still
    // attached before starting. See the note in the implementation.
    [[nodiscard]] int start(const QUuid& uuid, bool first_boot_done = false);

    // Rewrite the persisted domain XML for a VM that already exists,
    // without starting it.
    //
    // Settings like cores, memory and the shared folders are not
    // properties of a running machine and cannot be told to one: they
    // are the domain definition. The manager stops the guest, calls
    // this, and starts it again, which is the whole of what "apply and
    // restart" means.
    [[nodiscard]] int redefine(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    // ACPI shutdown request (``virDomainShutdown``). The guest may
    // ignore — pair with a UI timeout that escalates to ``destroy``.
    [[nodiscard]] int shutdown(const QUuid& uuid);

    // Hard kill (``virDomainDestroy``).
    [[nodiscard]] int force_off(const QUuid& uuid);

    // Undefine (delete persistent config, NOT the disk).
    [[nodiscard]] int undefine(const QUuid& uuid);

    // Make room for a new machine called ``name``: undefine the domain
    // of that name, with its firmware variables and any saved state,
    // unless it is running — then nothing is touched and ``error_out``
    // says to shut it down first. True when there was nothing to remove.
    [[nodiscard]] bool remove_existing(const QString& name, QString* error_out);

    // Whether libvirt knows about this VM and it's currently running.
    [[nodiscard]] bool is_running(const QUuid& uuid) const;

    // The domain's definition as libvirt has it: the running domain's
    // live one, or with ``stored`` the persistent one the next start
    // boots (which a redefine under a running guest has already
    // replaced). Empty when there is no such domain.
    [[nodiscard]] QString domain_xml(const QUuid& uuid, bool stored = false) const;

    // The live VNC graphics port of the running domain (the
    // ``<graphics type='vnc' autoport='yes'>`` libvirt assigned), or
    // -1 if the domain isn't up / has none yet. Used by the manager's
    // live-preview surface.
    [[nodiscard]] int vnc_port(const QUuid& uuid) const;

    // Eject the install media (Windows ISO sdb, payload ISO sdc) once
    // install + setup are done. Only the CD-ROM *media* is removed; the
    // cdrom devices and the NVMe system disk are untouched.
    // Applied LIVE+CONFIG when running, CONFIG-only otherwise.
    // Best-effort: a missing/already-empty drive is not an error, and
    // neither is a missing domain, so there is nothing to return —
    // each drive's outcome is logged.
    void eject_install_media(const QUuid& uuid);

    // Change the resolutions a running guest's display offers, with no
    // restart: ``modes`` becomes the GPU device's ``modes`` property
    // (QMP qom-set on /machine/peripheral/<DomainXmlBuilder::
    // gpu_device_id()>). WxH entries between commas, the first
    // preferred, e.g. "1920x1080"; empty is the host monitors' own
    // modes. The guest's driver re-reads its monitor, and when the list
    // lacks the mode Windows is showing it switches to the first one, so
    // offering one size sets the resolution.
    //
    // Only the running device changes: the definition keeps whatever it
    // was defined with until the next redefine. False, with the reason
    // in ``error_out`` when given, if the VM is not running, the list is
    // malformed, or QEMU refused it (a domain defined before the GPU
    // device existed has no such device).
    [[nodiscard]] bool set_display_modes(const QUuid& uuid, const QString& modes,
                                         QString* error_out);

    // What the running GPU device's ``modes`` property holds now (QMP
    // qom-get on the same path): the list it offers, "" for the host
    // monitors' own modes. nullopt, with the reason in ``error_out`` when
    // given, if the VM is not running or QEMU would not say.
    [[nodiscard]] std::optional<QString> display_modes(const QUuid& uuid,
                                                       QString* error_out) const;

private:
    // Record libvirt's error for ``call``, log it, and return -1.
    int fail(const char* call);
    QString last_error_;

    [[nodiscard]] DomainHandle lookup(const QUuid& uuid) const;
    // One QMP command on ``uuid``'s running QEMU, through libvirt's
    // monitor passthrough: its ``return`` member, or nullopt with the
    // reason in ``error_out``.
    [[nodiscard]] std::optional<QJsonValue> qmp(const QUuid& uuid, const QJsonObject& command,
                                                QString* error_out) const;
    // ``inputs`` with every empty accel-virt path filled in from
    // platform::AccelVirtPaths. False, logged, when QEMU or the executor
    // cannot be found: a domain without them does not start, or starts
    // with a GPU that accelerates nothing, and either reads as a fault
    // somewhere else.
    [[nodiscard]] bool fill_accel_virt(DomainXmlInputs& inputs);

    LibvirtConnection& conn_;
};

}  // namespace lsw::host::vm
