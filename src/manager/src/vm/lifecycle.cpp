#include "lsw/manager/vm/lifecycle.hpp"

#include "lsw/log.hpp"

#include <QProcess>
#include <QRegularExpression>
#include <libvirt/libvirt.h>

namespace lsw::manager::vm {

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
    const QString xml = DomainXmlBuilder::build(vm, inputs);
    auto* d = ::virDomainDefineXML(conn_.raw(), xml.toUtf8().constData());
    if (d == nullptr) {
        LSW_ERROR("virDomainDefineXML failed: {}", conn_.last_error().toStdString());
        return -1;
    }
    DomainHandle dom{d};
    if (::virDomainCreate(dom.raw()) < 0) {
        LSW_ERROR("virDomainCreate failed: {}", conn_.last_error().toStdString());
        return -1;
    }
    LSW_INFO("VM started: {} (cid={})", vm.name.toStdString(), vm.vsock_cid);
    return 0;
}

int Lifecycle::start(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        LSW_ERROR("start: domain {} not found", uuid.toString().toStdString());
        return -1;
    }
    if (::virDomainIsActive(dom.raw()) == 1) {
        return 0;  // already running — idempotent
    }
    if (::virDomainCreate(dom.raw()) < 0) {
        LSW_ERROR("virDomainCreate failed: {}", conn_.last_error().toStdString());
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
    return ::virDomainShutdown(dom.raw());
}

int Lifecycle::force_off(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return -1;
    }
    return ::virDomainDestroy(dom.raw());
}

int Lifecycle::undefine(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return -1;
    }
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

}  // namespace lsw::manager::vm
