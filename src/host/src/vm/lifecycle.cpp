#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/vm/ivshmem_server.hpp"
#include "lsw/host/vm/shmem_region.hpp"

#include <QProcess>
#include <QRegularExpression>
#include <libvirt/libvirt.h>

#include <cstdint>

namespace lsw::host::vm {

namespace {

[[nodiscard]] QByteArray uuid_bytes(const QUuid& u) {
    // libvirt wants the 16 raw bytes; QUuid::toRfc4122 gives them.
    return u.toRfc4122();
}

}  // namespace

Lifecycle::Lifecycle(LibvirtConnection& conn, QObject* parent) : QObject(parent), conn_(conn) {}

int Lifecycle::create_disk(const QString& path, int size_gib) {
    // ``qemu-img create -f qcow2 path SIZE`` — sparse by default.
    QProcess p;
    p.start("qemu-img", {"create", "-f", "qcow2", path, QStringLiteral("%1G").arg(size_gib)});
    if (!p.waitForFinished(30'000) || p.exitCode() != 0) {
        LSW_ERROR("qemu-img create {} {}G failed: {}", path.toStdString(), size_gib,
                  QString::fromUtf8(p.readAllStandardError()).toStdString());
        return -1;
    }
    LSW_INFO("created qcow2 disk {} ({} GiB sparse)", path.toStdString(), size_gib);
    return 0;
}

int Lifecycle::define_and_start(const config::VmRecord& vm, const DomainXmlInputs& inputs) {
    if (!conn_.is_open()) {
        LSW_ERROR("libvirt not connected");
        return -1;
    }
    // The IVSHMEM server socket MUST exist before virDomainCreate or
    // QEMU's chardev setup fails outright. We bring up the region +
    // server here, then pass the socket path to the XML builder.
    DomainXmlInputs filled = inputs;
    const QString sock = ensure_shmem(vm.uuid, filled.ivshmem_size_mib);
    if (sock.isEmpty()) {
        return -1;
    }
    filled.ivshmem_socket = sock;

    const QString xml = DomainXmlBuilder::build(vm, filled);
    auto* d = ::virDomainDefineXML(conn_.raw(), xml.toUtf8().constData());
    if (d == nullptr) {
        LSW_ERROR("virDomainDefineXML failed: {}", conn_.last_error().toStdString());
        teardown_shmem(vm.uuid);
        return -1;
    }
    DomainHandle dom{d};
    if (::virDomainCreate(dom.raw()) < 0) {
        LSW_ERROR("virDomainCreate failed: {}", conn_.last_error().toStdString());
        teardown_shmem(vm.uuid);
        return -1;
    }
    LSW_INFO("VM started: {} (cid={})", vm.name.toStdString(), vm.vsock_cid);
    return 0;
}

int Lifecycle::start(const QUuid& uuid, bool first_boot_done) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        LSW_ERROR("start: domain {} not found", uuid.toString().toStdString());
        return -1;
    }
    if (::virDomainIsActive(dom.raw()) == 1) {
        // Idempotent — but a VM that is already up was started by some
        // earlier manager, so attach to the region it left rather than
        // running blind.
        if (!shmem_.contains(uuid)) {
            (void)attach_running(uuid);
        }
        return 0;
    }
    // A VM whose guest has already run has no business still holding
    // the installer.
    //
    // Provisioning ejects the media when it finishes, but that only
    // covers a run that reached the end WITH that step present: a VM
    // installed before it existed, or one whose run failed partway and
    // was picked up by hand, keeps the ISOs for good. The guest then
    // shows the install media as drives every boot, and the files stay
    // pinned. Doing it here as well makes it self-correcting, and it
    // costs one no-op call per start otherwise.
    if (first_boot_done) {
        (void)eject_install_media(uuid);
    }

    // The VM's persistent XML already references the chardev path;
    // ensure the matching server is up before QEMU connects to it.
    if (ensure_shmem(uuid, 256).isEmpty()) {
        return -1;
    }
    if (::virDomainCreate(dom.raw()) < 0) {
        LSW_ERROR("virDomainCreate failed: {}", conn_.last_error().toStdString());
        teardown_shmem(uuid);
        return -1;
    }
    return 0;
}

DomainHandle Lifecycle::lookup(const QUuid& uuid) const {
    if (!conn_.is_open()) {
        return {};
    }
    const auto bytes = uuid_bytes(uuid);
    auto* d = ::virDomainLookupByUUID(conn_.raw(),
                                      reinterpret_cast<const unsigned char*>(bytes.constData()));
    return DomainHandle{d};
}

int Lifecycle::shutdown(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return -1;
    }
    // Don't tear shmem here — ACPI shutdown is asynchronous; the
    // server stays up so we can still service late doorbells from a
    // guest that hasn't yet acted on the shutdown signal. The
    // ``force_off`` path is what reaps the region.
    return ::virDomainShutdown(dom.raw());
}

int Lifecycle::force_off(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return -1;
    }
    const int rc = ::virDomainDestroy(dom.raw());
    teardown_shmem(uuid);
    return rc;
}

int Lifecycle::undefine(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return -1;
    }
    teardown_shmem(uuid);
    return ::virDomainUndefine(dom.raw());
}

bool Lifecycle::is_running(const QUuid& uuid) const {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return false;
    }
    int active = ::virDomainIsActive(dom.raw());
    return active == 1;
}

int Lifecycle::vnc_port(const QUuid& uuid) const {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return -1;
    }
    char* xml = ::virDomainGetXMLDesc(dom.raw(), 0);
    if (xml == nullptr) {
        return -1;
    }
    const QString text = QString::fromUtf8(xml);
    ::free(xml);
    static const QRegularExpression re(
        QStringLiteral("<graphics type='vnc'[^>]*port='(-?\\d+)'"));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return -1;
    }
    bool ok = false;
    const int port = m.captured(1).toInt(&ok);
    return (ok && port > 0) ? port : -1;
}

int Lifecycle::eject_install_media(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        LSW_ERROR("eject: domain {} not found", uuid.toString().toStdString());
        return -1;
    }
    unsigned int flags = VIR_DOMAIN_AFFECT_CONFIG;
    if (::virDomainIsActive(dom.raw()) == 1) {
        flags |= VIR_DOMAIN_AFFECT_LIVE;
    }
    // A <disk device='cdrom'> with no <source> = ejected media.
    // virDomainUpdateDeviceFlags matches the existing device by
    // <target dev=...>; the system disk (vda, virtio) is never named
    // here so it can't be affected.
    static const char* kTargets[] = {"sdb", "sdc", "sdd"};
    int rc = 0;
    for (const char* tgt : kTargets) {
        const QString xml =
            QStringLiteral("<disk type='file' device='cdrom'>"
                           "<driver name='qemu' type='raw'/>"
                           "<target dev='%1' bus='sata'/>"
                           "<readonly/></disk>")
                .arg(QString::fromLatin1(tgt));
        if (::virDomainUpdateDeviceFlags(dom.raw(), xml.toUtf8().constData(),
                                         flags) < 0) {
            // Not fatal — the drive may already be empty, or absent.
            LSW_WARN("eject: {} not ejected ({})", tgt,
                     conn_.last_error().toStdString());
        } else {
            LSW_INFO("eject: {} media removed", tgt);
        }
    }
    return rc;
}

ShmemRegion* Lifecycle::shmem_region(const QUuid& uuid) const {
    auto it = shmem_.find(uuid);
    return (it != shmem_.end()) ? it->second.region.get() : nullptr;
}

IvshmemServer* Lifecycle::ivshmem_server(const QUuid& uuid) const {
    auto it = shmem_.find(uuid);
    return (it != shmem_.end()) ? it->second.server.get() : nullptr;
}

bool Lifecycle::attach_running(const QUuid& uuid) {
    if (shmem_.contains(uuid)) {
        return true;
    }
    if (!is_running(uuid)) {
        return false;
    }
    ShmemBundle bundle;
    bundle.region = std::make_unique<ShmemRegion>();
    if (bundle.region->attach(uuid) != 0) {
        LSW_WARN("attach: VM {} is running but its shared region is gone — window "
                 "mirrors will be black until the VM is restarted from here",
                 uuid.toString().toStdString());
        return false;
    }
    // No IvshmemServer: QEMU handshook its device against the socket and
    // eventfds of whichever manager started the VM, and that handshake
    // is one-shot — a new server here would never be connected to. The
    // doorbells are therefore gone for this session, and consumers fall
    // back to polling the region. Everything else about it is intact,
    // because the memory is QEMU's for as long as the domain lives.
    LSW_INFO("attach: reattached to the running VM {} — no frame doorbells this "
             "session (the VM was started by an earlier manager), so frames are "
             "polled instead",
             uuid.toString().toStdString());
    // A RenderServer, though, because none of the above applies to it.
    // It is a child process of the manager, so the one that was serving
    // this domain died with the manager that started it; and its region
    // is the second ivshmem device — a plain memory-backend-file that
    // QEMU maps directly, with no handshake to have missed. A fresh
    // server opens that same file and serves it.
    //
    // Without this, a manager restarted while its VM kept running left
    // the guest attaching to a region nobody was consuming: every
    // application reached "attached to render server at ivshmem-0" and
    // then timed out on its first command with "head is 0", which reads
    // like a driver fault and is not one.
    bundle.render = std::make_unique<RenderServer>();
    (void)bundle.render->ensure_running(uuid);
    shmem_.emplace(uuid, std::move(bundle));
    Q_EMIT shmem_ready(uuid);
    return true;
}

QString Lifecycle::ensure_shmem(const QUuid& uuid, int size_mib) {
    // Reuse an existing bundle (start-without-define on an already
    // running VM hits this path). The shm region + server stay live
    // across VM ACPI shutdowns so late frames don't crash the manager.
    if (auto it = shmem_.find(uuid); it != shmem_.end()) {
        if (it->second.server) {
            return it->second.server->socket_path();
        }
        // An attached bundle: no server, and the region belongs to a VM
        // that is about to be started again. Both have to be made
        // fresh — the guest that was using it is gone, and the new QEMU
        // needs a socket to hand shake against.
        LSW_INFO("shmem: replacing the attached region for {} with a fresh one for "
                 "the new VM run",
                 uuid.toString().toStdString());
        teardown_shmem(uuid);
    }
    ShmemBundle bundle;
    bundle.region = std::make_unique<ShmemRegion>();
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(size_mib > 0 ? size_mib : 256)
        * 1024ULL * 1024ULL;
    if (bundle.region->open(uuid, bytes) != 0) {
        return {};
    }
    bundle.server = std::make_unique<IvshmemServer>(uuid, *bundle.region);
    if (bundle.server->start() != 0) {
        bundle.region->close();
        return {};
    }
    // Not fatal, and deliberately so: a guest that boots and reports no
    // GPU driver can be looked at, and one that will not start cannot.
    // The warning is in RenderServer.
    bundle.render = std::make_unique<RenderServer>();
    (void)bundle.render->ensure_running(uuid);
    const QString sock = bundle.server->socket_path();
    shmem_.emplace(uuid, std::move(bundle));
    Q_EMIT shmem_ready(uuid);
    return sock;
}

void Lifecycle::teardown_shmem(const QUuid& uuid) {
    auto it = shmem_.find(uuid);
    if (it == shmem_.end()) {
        return;
    }
    if (it->second.render) {
        it->second.render->stop();
    }
    if (it->second.server) {
        it->second.server->stop();
    }
    if (it->second.region) {
        // Unlink, not just unmap: this path runs when the VM is off or
        // failed to start, and a region no VM is using should not be
        // left behind for the next manager to attach to.
        it->second.region->discard();
    }
    shmem_.erase(it);
    // After the erase: a consumer that re-queries in the slot handler
    // must see the bundle already gone, not a dangling pointer.
    Q_EMIT shmem_gone(uuid);
}

}  // namespace lsw::host::vm
