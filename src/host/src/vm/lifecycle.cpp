#include "lsw/host/vm/lifecycle.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/platform/tool_process.hpp"
#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/export_socket.hpp"

#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <libvirt/libvirt-qemu.h>
#include <libvirt/libvirt.h>

namespace lsw::host::vm {

Lifecycle::Lifecycle(LibvirtConnection& conn, QObject* parent) : QObject(parent), conn_(conn) {}

int Lifecycle::create_disk(const QString& path, int size_gib) {
    // ``qemu-img create -f qcow2 path SIZE`` — sparse by default.
    QString error;
    if (!platform::run_tool(QStringLiteral("qemu-img"),
                            {QStringLiteral("create"), QStringLiteral("-f"),
                             QStringLiteral("qcow2"), path, QStringLiteral("%1G").arg(size_gib)},
                            30'000, nullptr, &error)) {
        LSW_ERROR("qemu-img create {} {}G failed: {}", path.toStdString(), size_gib,
                  error.toStdString());
        return -1;
    }
    LSW_INFO("created qcow2 disk {} ({} GiB sparse)", path.toStdString(), size_gib);
    return 0;
}

bool Lifecycle::grow_disk(const QString& path, int size_gib, QString* error_out) {
    const quint64 target = static_cast<quint64>(size_gib) << 30;
    const auto current = DiskImage::virtual_size(path);
    if (!current) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("could not read the size of %1").arg(path);
        }
        return false;
    }
    if (target <= *current) {
        return true;
    }
    if (!platform::run_tool(QStringLiteral("qemu-img"),
                            {QStringLiteral("resize"), QStringLiteral("-f"),
                             QStringLiteral("qcow2"), path, QStringLiteral("%1G").arg(size_gib)},
                            60'000, nullptr, error_out)) {
        LSW_ERROR("qemu-img resize {} {}G failed", path.toStdString(), size_gib);
        return false;
    }
    LSW_INFO("grew {} from {} to {} GiB", path.toStdString(), *current >> 30, size_gib);
    return true;
}

int Lifecycle::fail(const char* call) {
    last_error_ = conn_.last_error();
    LSW_ERROR("{} failed: {}", call, last_error_.toStdString());
    return -1;
}

int Lifecycle::define_and_start(const config::VmRecord& vm, const DomainXmlInputs& inputs) {
    if (!conn_.is_open()) {
        LSW_ERROR("libvirt not connected");
        return -1;
    }
    DomainXmlInputs filled = inputs;
    if (!fill_accel_virt(filled)) {
        return -1;
    }
    const QString xml = DomainXmlBuilder::build(vm, filled);
    auto* d = ::virDomainDefineXML(conn_.raw(), xml.toUtf8().constData());
    if (d == nullptr) {
        return fail("virDomainDefineXML");
    }
    DomainHandle dom{d};
    if (::virDomainCreate(dom.raw()) < 0) {
        return fail("virDomainCreate");
    }
    LSW_INFO("VM started: {} (cid={})", vm.name.toStdString(), vm.vsock_cid);
    return 0;
}

int Lifecycle::redefine(const config::VmRecord& vm, const DomainXmlInputs& inputs) {
    if (!conn_.is_open()) {
        LSW_ERROR("libvirt not connected");
        return -1;
    }
    DomainXmlInputs filled = inputs;
    if (!fill_accel_virt(filled)) {
        return -1;
    }
    const QString xml = DomainXmlBuilder::build(vm, filled);
    auto* d = ::virDomainDefineXML(conn_.raw(), xml.toUtf8().constData());
    if (d == nullptr) {
        return fail("virDomainDefineXML");
    }
    DomainHandle dom{d};
    LSW_INFO("VM redefined: {}", vm.name.toStdString());
    return 0;
}

int Lifecycle::start(const QUuid& uuid, bool first_boot_done) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        LSW_ERROR("start: domain {} not found", uuid.toString().toStdString());
        return -1;
    }
    if (::virDomainIsActive(dom.raw()) == 1) {
        return 0;  // idempotent
    }
    // A VM whose guest has already run has no business still holding
    // the installer.
    //
    // Provisioning ejects the media when it finishes, but that only
    // covers a run that reached the end: one that failed partway and
    // was picked up by hand keeps the ISOs for good. The guest then
    // shows the install media as drives every boot, and the files stay
    // pinned. Doing it here as well makes it self-correcting, and it
    // costs one no-op call per start otherwise.
    if (first_boot_done) {
        eject_install_media(uuid);
    }
    if (!retarget_host(uuid)) {
        return -1;
    }

    if (::virDomainCreate(dom.raw()) < 0) {
        return fail("virDomainCreate");
    }
    return 0;
}

bool Lifecycle::retarget_host(const QUuid& uuid) {
    DomainXmlInputs own;
    if (!fill_accel_virt(own)) {
        return false;
    }
    QDomDocument doc;
    if (!doc.setContent(domain_xml(uuid, /*stored=*/true))) {
        return true;  // no stored definition to correct; the start says why
    }
    const QDomElement root = doc.documentElement();
    QDomElement commandline = root.firstChildElement(QStringLiteral("qemu:commandline"));
    if (commandline.isNull()) {
        return true;  // not an accel-virt domain
    }
    QStringList changed;
    const auto set_text = [&changed](QDomElement element, const QString& value, const char* what) {
        if (element.isNull() || value.isEmpty() || element.text() == value) {
            return;
        }
        while (element.hasChildNodes()) {
            element.removeChild(element.firstChild());
        }
        element.appendChild(element.ownerDocument().createTextNode(value));
        changed << QLatin1String(what);
    };
    const auto set_value = [&changed](QDomElement element, const QString& value, const char* what) {
        if (value.isEmpty() || element.attribute(QStringLiteral("value")) == value) {
            return;
        }
        element.setAttribute(QStringLiteral("value"), value);
        changed << QLatin1String(what);
    };

    set_text(root.firstChildElement(QStringLiteral("devices")).firstChildElement(QStringLiteral("emulator")),
             own.qemu_binary, "QEMU");

    static const QRegularExpression executor(QStringLiteral("(executor=)[^,]*"));
    QDomElement data_flag;
    QDomElement data_dir;
    for (QDomElement e = commandline.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        const QString name = e.attribute(QStringLiteral("name"));
        const QString value = e.attribute(QStringLiteral("value"));
        if (e.tagName() == QStringLiteral("qemu:env") && name == QStringLiteral("QEMU_MODULE_DIR")) {
            set_value(e, own.qemu_module_dir, "QEMU's modules");
        } else if (e.tagName() == QStringLiteral("qemu:env") && name == QStringLiteral("AV_D3D_PATH")) {
            set_value(e, own.d3d_lib_dir, "DXVK and vkd3d-proton");
        } else if (e.tagName() == QStringLiteral("qemu:arg") && value.contains(QStringLiteral("executor="))) {
            QString retargeted = value;
            retargeted.replace(executor, QStringLiteral("\\1") + own.executor);
            set_value(e, retargeted, "the executor");
        } else if (e.tagName() == QStringLiteral("qemu:arg") && value == QStringLiteral("-L")) {
            data_flag = e;
            data_dir = e.nextSiblingElement();
        }
    }
    // An installed QEMU finds its firmware through -L; a build tree's
    // finds its own, and carries none.
    if (!data_flag.isNull() && own.qemu_data_dir.isEmpty()) {
        commandline.removeChild(data_dir);
        commandline.removeChild(data_flag);
        changed << QStringLiteral("QEMU's firmware");
    } else if (!data_flag.isNull()) {
        set_value(data_dir, own.qemu_data_dir, "QEMU's firmware");
    } else if (!own.qemu_data_dir.isEmpty()) {
        for (const QString& arg : {QStringLiteral("-L"), own.qemu_data_dir}) {
            QDomElement e = doc.createElement(QStringLiteral("qemu:arg"));
            e.setAttribute(QStringLiteral("value"), arg);
            commandline.appendChild(e);
        }
        changed << QStringLiteral("QEMU's firmware");
    }

    if (changed.isEmpty()) {
        return true;
    }
    changed.removeDuplicates();
    auto* d = ::virDomainDefineXML(conn_.raw(), doc.toString(2).toUtf8().constData());
    if (d == nullptr) {
        return fail("virDomainDefineXML") == 0;
    }
    DomainHandle redefined{d};
    LSW_INFO("VM pointed at this LSW's host files ({}): {}", changed.join(QStringLiteral(", ")).toStdString(),
             own.qemu_binary.toStdString());
    return true;
}

DomainHandle Lifecycle::lookup(const QUuid& uuid) const {
    if (!conn_.is_open()) {
        return {};
    }
    return DomainHandle::lookup(conn_.raw(), uuid);
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

bool Lifecycle::remove_existing(const QString& name, QString* error_out) {
    const auto refuse = [this, error_out](const QString& why) {
        last_error_ = why;
        LSW_ERROR("{}", why.toStdString());
        if (error_out != nullptr) {
            *error_out = why;
        }
        return false;
    };
    if (!conn_.is_open()) {
        return refuse(QStringLiteral("libvirt is not connected"));
    }
    DomainHandle dom{::virDomainLookupByName(conn_.raw(), name.toUtf8().constData())};
    if (!dom.valid()) {
        return true;
    }
    // Its disk is about to be recreated at the same path, which under a
    // running QEMU would be writing over a disk in use.
    if (::virDomainIsActive(dom.raw()) == 1) {
        return refuse(QStringLiteral("the existing virtual machine '%1' is running; shut it "
                                     "down before installing a new one")
                          .arg(name));
    }
    const unsigned int flags = VIR_DOMAIN_UNDEFINE_NVRAM | VIR_DOMAIN_UNDEFINE_MANAGED_SAVE
                               | VIR_DOMAIN_UNDEFINE_SNAPSHOTS_METADATA
                               | VIR_DOMAIN_UNDEFINE_CHECKPOINTS_METADATA;
    if (::virDomainUndefineFlags(dom.raw(), flags) < 0) {
        return refuse(QStringLiteral("could not remove the existing virtual machine '%1': %2")
                          .arg(name, conn_.last_error()));
    }
    LSW_INFO("removed the existing domain '{}' to install a new one", name.toStdString());
    return true;
}

int Lifecycle::domain_id(const QUuid& uuid) const {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return -1;
    }
    const unsigned int id = ::virDomainGetID(dom.raw());
    return id == static_cast<unsigned int>(-1) ? -1 : static_cast<int>(id);
}

bool Lifecycle::is_running(const QUuid& uuid) const {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return false;
    }
    int active = ::virDomainIsActive(dom.raw());
    return active == 1;
}

QString Lifecycle::domain_xml(const QUuid& uuid, bool stored) const {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        return {};
    }
    char* xml = ::virDomainGetXMLDesc(dom.raw(), stored ? VIR_DOMAIN_XML_INACTIVE : 0);
    if (xml == nullptr) {
        return {};
    }
    QString out = QString::fromUtf8(xml);
    ::free(xml);  // NOLINT(cppcoreguidelines-no-malloc): libvirt's allocation
    return out;
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
    static const QRegularExpression re(QStringLiteral("<graphics type='vnc'[^>]*port='(-?\\d+)'"));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return -1;
    }
    bool ok = false;
    const int port = m.captured(1).toInt(&ok);
    return (ok && port > 0) ? port : -1;
}

void Lifecycle::eject_install_media(const QUuid& uuid) {
    auto dom = lookup(uuid);
    if (!dom.valid()) {
        LSW_ERROR("eject: domain {} not found", uuid.toString().toStdString());
        return;
    }
    unsigned int flags = VIR_DOMAIN_AFFECT_CONFIG;
    if (::virDomainIsActive(dom.raw()) == 1) {
        flags |= VIR_DOMAIN_AFFECT_LIVE;
    }
    // A <disk device='cdrom'> with no <source> = ejected media.
    // virDomainUpdateDeviceFlags matches the existing device by
    // <target dev=...>; the system disk (nvme0n1) is never named here
    // so it can't be affected.
    static const char* kTargets[] = {"sdb", "sdc"};
    for (const char* tgt : kTargets) {
        const QString xml = QStringLiteral("<disk type='file' device='cdrom'>"
                                           "<driver name='qemu' type='raw'/>"
                                           "<target dev='%1' bus='sata'/>"
                                           "<readonly/></disk>")
                                .arg(QString::fromLatin1(tgt));
        if (::virDomainUpdateDeviceFlags(dom.raw(), xml.toUtf8().constData(), flags) < 0) {
            // Not fatal — the drive may already be empty, or absent.
            LSW_WARN("eject: {} not ejected ({})", tgt, conn_.last_error().toStdString());
        } else {
            LSW_INFO("eject: {} media removed", tgt);
        }
    }
}

bool Lifecycle::fill_accel_virt(DomainXmlInputs& inputs) {
    const auto fill = [](QString& field, QString (*locate)()) {
        if (field.isEmpty()) {
            field = locate();
        }
    };
    fill(inputs.qemu_binary, &platform::AccelVirtPaths::qemu_binary);
    fill(inputs.executor, &platform::AccelVirtPaths::executor);
    fill(inputs.d3d_lib_dir, &platform::AccelVirtPaths::d3d_lib_dir);
    fill(inputs.export_socket, &ExportSocket::path);
    if (inputs.shader_cache_dir.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                            QStringLiteral("/lsw/shader-cache");
        if (QDir().mkpath(dir)) {
            inputs.shader_cache_dir = dir;
        } else {
            LSW_WARN("shader cache: {} could not be made; shaders recompile every boot", dir.toStdString());
        }
    }

    // QEMU's modules are beside it, whichever build it came from.
    if (inputs.qemu_module_dir.isEmpty() && !inputs.qemu_binary.isEmpty()) {
        inputs.qemu_module_dir = QFileInfo(inputs.qemu_binary).absolutePath();
    }
    // Its firmware and ROMs are beside it only when it is installed; a
    // build tree's QEMU finds its own, and this stays empty.
    fill(inputs.qemu_data_dir, &platform::AccelVirtPaths::qemu_data_dir);
    // The distribution's QEMU cannot load the GPU device, so there is no
    // falling back to it: the domain would not start, and libvirt's
    // reason names a device rather than a missing build.
    if (inputs.qemu_binary.isEmpty()) {
        last_error_ = QStringLiteral(
            "accel-virt's QEMU was not found: build the accel_virt_host target, or point "
            "LSW_AV_QEMU_BUILD at a build that has qemu/qemu-system-x86_64");
        LSW_ERROR("{}", last_error_.toStdString());
        return false;
    }
    // Without an executor the device is a plain 2D virtio-gpu with no
    // av capset, and the guest's driver has nothing to drive.
    if (inputs.executor.isEmpty()) {
        last_error_ = QStringLiteral(
            "accel-virt's libav_executor.so was not found: build the accel_virt_host target, "
            "or point LSW_AV_QEMU_BUILD at a build that has it");
        LSW_ERROR("{}", last_error_.toStdString());
        return false;
    }
    if (inputs.d3d_lib_dir.isEmpty()) {
        LSW_WARN("DXVK / vkd3d-proton were not found (accel_virt_d3d, or LSW_AV_D3D_PATH): the "
                 "host serves no Direct3D, and the guest desktop will crash-loop");
    }
    return true;
}

QString Lifecycle::usb_hostdev_xml(const DomainDescription::UsbDevice& device) {
    return QStringLiteral("<hostdev mode='subsystem' type='usb'><source>"
                          "<vendor id='0x%1'/><product id='0x%2'/>"
                          "<address bus='%3' device='%4'/></source></hostdev>")
        .arg(device.vendor, 4, 16, QLatin1Char('0'))
        .arg(device.product, 4, 16, QLatin1Char('0'))
        .arg(device.bus)
        .arg(device.device);
}

bool Lifecycle::attach_usb(const QUuid& uuid, const DomainDescription::UsbDevice& device,
                           QString* error_out) {
    auto dom = lookup(uuid);
    const QString xml = usb_hostdev_xml(device);
    if (dom.valid() && ::virDomainAttachDeviceFlags(dom.raw(), xml.toUtf8().constData(),
                                                    VIR_DOMAIN_AFFECT_LIVE) == 0) {
        return true;
    }
    const QString why = dom.valid() ? conn_.last_error() : QStringLiteral("the guest is not running");
    LSW_WARN("attach USB {:04x}:{:04x} (bus {} device {}): {}", device.vendor, device.product,
             device.bus, device.device, why.toStdString());
    if (error_out != nullptr) {
        *error_out = why;
    }
    return false;
}

bool Lifecycle::detach_usb(const QUuid& uuid, const DomainDescription::UsbDevice& device,
                           QString* error_out) {
    auto dom = lookup(uuid);
    const QString xml = usb_hostdev_xml(device);
    if (dom.valid() && ::virDomainDetachDeviceFlags(dom.raw(), xml.toUtf8().constData(),
                                                    VIR_DOMAIN_AFFECT_LIVE) == 0) {
        return true;
    }
    const QString why = dom.valid() ? conn_.last_error() : QStringLiteral("the guest is not running");
    LSW_WARN("detach USB {:04x}:{:04x} (bus {} device {}): {}", device.vendor, device.product,
             device.bus, device.device, why.toStdString());
    if (error_out != nullptr) {
        *error_out = why;
    }
    return false;
}

bool Lifecycle::set_display_modes(const QUuid& uuid, const QString& modes, QString* error_out) {
    const auto fail = [error_out](const QString& why) {
        LSW_WARN("display modes: {}", why.toStdString());
        if (error_out != nullptr) {
            *error_out = why;
        }
        return false;
    };
    // Checked here rather than left to QEMU because the device takes any
    // string and the guest's driver then quietly drops what it cannot
    // parse, which reads as a resolution change that did nothing.
    static const QRegularExpression list(
        QStringLiteral("^(\\d{1,5}x\\d{1,5})(,\\d{1,5}x\\d{1,5})*$"));
    const QString value = modes.trimmed().toLower();
    if (!value.isEmpty() && !list.match(value).hasMatch()) {
        return fail(QStringLiteral("'%1' is not a list of WxH sizes").arg(modes));
    }
    // Built as JSON rather than spliced into a string, so the value is
    // quoted by the one thing that knows how.
    const QJsonObject command{
        {QStringLiteral("execute"), QStringLiteral("qom-set")},
        {QStringLiteral("arguments"),
         QJsonObject{
             {QStringLiteral("path"),
              QStringLiteral("/machine/peripheral/%1").arg(DomainXmlBuilder::gpu_device_id())},
             {QStringLiteral("property"), QStringLiteral("modes")},
             {QStringLiteral("value"), value},
         }},
    };
    QString why;
    if (!qmp(uuid, command, &why)) {
        return fail(why);
    }
    LSW_INFO("display modes: now '{}'",
             value.isEmpty() ? std::string("the host monitors' modes") : value.toStdString());
    return true;
}

std::optional<QString> Lifecycle::display_modes(const QUuid& uuid, QString* error_out) const {
    const QJsonObject command{
        {QStringLiteral("execute"), QStringLiteral("qom-get")},
        {QStringLiteral("arguments"),
         QJsonObject{
             {QStringLiteral("path"),
              QStringLiteral("/machine/peripheral/%1").arg(DomainXmlBuilder::gpu_device_id())},
             {QStringLiteral("property"), QStringLiteral("modes")},
         }},
    };
    const auto answer = qmp(uuid, command, error_out);
    if (!answer) {
        return std::nullopt;
    }
    return answer->toString();
}

std::optional<QJsonValue> Lifecycle::qmp(const QUuid& uuid, const QJsonObject& command,
                                         QString* error_out) const {
    const auto fail = [error_out](const QString& why) -> std::optional<QJsonValue> {
        if (error_out != nullptr) {
            *error_out = why;
        }
        return std::nullopt;
    };
    auto dom = lookup(uuid);
    if (!dom.valid() || ::virDomainIsActive(dom.raw()) != 1) {
        return fail(QStringLiteral("the VM is not running"));
    }
    const QByteArray json = QJsonDocument(command).toJson(QJsonDocument::Compact);
    // libvirt marks the domain tainted ("custom-monitor") the first time
    // this runs. That is a note in its log, not a change in behaviour,
    // and there is no libvirt API for a property of a device it does
    // not know.
    char* reply = nullptr;
    if (::virDomainQemuMonitorCommand(dom.raw(), json.constData(), &reply,
                                      VIR_DOMAIN_QEMU_MONITOR_COMMAND_DEFAULT) < 0) {
        return fail(QStringLiteral("QEMU refused it: %1").arg(conn_.last_error()));
    }
    const QJsonObject answer = QJsonDocument::fromJson(QByteArray(reply)).object();
    ::free(reply);
    if (answer.contains(QStringLiteral("error"))) {
        return fail(QStringLiteral("QEMU refused it: %1")
                        .arg(answer.value(QStringLiteral("error"))
                                 .toObject()
                                 .value(QStringLiteral("desc"))
                                 .toString()));
    }
    return answer.value(QStringLiteral("return"));
}

}  // namespace lsw::host::vm
