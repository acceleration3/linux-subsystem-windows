// The domain definition is built from strings a user chooses — shared
// folder paths above all — and the template quotes its attributes with
// '. A folder called "Tom's & Jerry's" used to go into the XML as it
// was, which is a definition libvirt refuses, with an error that names
// a line of generated XML rather than the folder.
//
// This pins the escaping, the single-pass substitution that keeps a
// substituted value from being substituted again, the GPU's shape
// (accel-virt's virtio-gpu-av-pci on its own QEMU, a shared memfd under
// the guest's RAM, no emulated adapter except during Windows setup), the
// devices Windows drives in-box (an NVMe disk, an e1000e NIC, no driver
// ISO) and the settings half of the definition. No libvirt, no VM.

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/vm/domain_xml_builder.hpp"

#include <QCoreApplication>
#include <QDomDocument>
#include <QHash>
#include <QStringList>
#include <QUuid>
#include <QVector>
#include <cstdio>
#include <cstdlib>

using lsw::host::config::HostShare;
using lsw::host::config::ManagerSettings;
using lsw::host::config::VmRecord;
using lsw::host::vm::DomainShare;
using lsw::host::vm::DomainXmlBuilder;
using lsw::host::vm::DomainXmlInputs;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

QDomDocument parse(const QString& xml, const char* label) {
    QDomDocument doc;
    const QDomDocument::ParseResult parsed = doc.setContent(xml);
    if (!parsed) {
        std::printf("       line %lld: %s\n", static_cast<long long>(parsed.errorLine),
                    qPrintable(parsed.errorMessage));
    }
    check(static_cast<bool>(parsed), label);
    return doc;
}

// Every <qemu:arg value=...>, in order, as QEMU will receive them.
QStringList qemu_args(const QDomDocument& doc) {
    QStringList out;
    const QDomNodeList args = doc.elementsByTagName(QStringLiteral("qemu:arg"));
    for (int i = 0; i < args.count(); ++i) {
        out << args.at(i).toElement().attribute(QStringLiteral("value"));
    }
    return out;
}

// The argument after each ``flag`` whose value starts with ``prefix``.
QStringList args_after(const QStringList& args, const QString& flag, const QString& prefix) {
    QStringList out;
    for (qsizetype i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) == flag && args.at(i + 1).startsWith(prefix)) {
            out << args.at(i + 1);
        }
    }
    return out;
}

QString qemu_env(const QDomDocument& doc, const QString& name) {
    const QDomNodeList envs = doc.elementsByTagName(QStringLiteral("qemu:env"));
    for (int i = 0; i < envs.count(); ++i) {
        const QDomElement e = envs.at(i).toElement();
        if (e.attribute(QStringLiteral("name")) == name) {
            return e.attribute(QStringLiteral("value"));
        }
    }
    return {};
}

// The <disk> elements of one ``device`` kind ("disk", "cdrom").
QVector<QDomElement> disks(const QDomDocument& doc, const QString& device) {
    QVector<QDomElement> out;
    const QDomNodeList all = doc.elementsByTagName(QStringLiteral("disk"));
    for (int i = 0; i < all.count(); ++i) {
        const QDomElement e = all.at(i).toElement();
        if (e.attribute(QStringLiteral("device")) == device) {
            out << e;
        }
    }
    return out;
}

// Every device is one Windows has an in-box driver for: the system disk
// on NVMe, the NIC an e1000e, and nothing that wants virtio-win, whose
// ISO is no longer attached. The same for both shapes of the domain.
void check_inbox_devices(const QDomDocument& doc) {
    const QVector<QDomElement> system = disks(doc, QStringLiteral("disk"));
    const QDomElement target = system.value(0).firstChildElement(QStringLiteral("target"));
    check(system.size() == 1 && target.attribute(QStringLiteral("bus")) == QStringLiteral("nvme")
              && target.attribute(QStringLiteral("dev")) == QStringLiteral("nvme0n1"),
          "the one system disk is on NVMe");
    bool nvme_controller = false;
    const QDomNodeList controllers = doc.elementsByTagName(QStringLiteral("controller"));
    for (int i = 0; i < controllers.count(); ++i) {
        nvme_controller = nvme_controller
                          || controllers.at(i).toElement().attribute(QStringLiteral("type"))
                                 == QStringLiteral("nvme");
    }
    check(nvme_controller, "there is an NVMe controller for it");
    check(!system.value(0).firstChildElement(QStringLiteral("serial")).text().isEmpty(),
          "the NVMe disk has the serial libvirt requires");

    const QVector<QDomElement> cdroms = disks(doc, QStringLiteral("cdrom"));
    bool sata_only = cdroms.size() == 2;
    for (const QDomElement& cdrom : cdroms) {
        sata_only = sata_only
                    && cdrom.firstChildElement(QStringLiteral("target")).attribute(
                           QStringLiteral("bus")) == QStringLiteral("sata");
    }
    check(sata_only, "two SATA CDs: the Windows ISO and the payload, no driver ISO");

    const QStringList args = qemu_args(doc);
    const QStringList nics = args_after(args, QStringLiteral("-device"), QStringLiteral("e1000e,"));
    check(nics.size() == 1 && nics.value(0).contains(QStringLiteral("netdev=lsw-net")),
          "the NIC is an e1000e on the user-net backend");
    check(args_after(args, QStringLiteral("-netdev"), QStringLiteral("user,id=lsw-net,"))
                  .value(0)
                  .contains(QStringLiteral("hostfwd=tcp:127.0.0.1:5102-:5102")),
          "the agent's port is forwarded");
    check(doc.elementsByTagName(QStringLiteral("interface")).isEmpty(),
          "libvirt adds no NIC of its own");
    check(doc.elementsByTagName(QStringLiteral("rng")).isEmpty(), "no RNG device");
    const QDomNodeList balloons = doc.elementsByTagName(QStringLiteral("memballoon"));
    check(balloons.count() == 1
              && balloons.at(0).toElement().attribute(QStringLiteral("model"))
                     == QStringLiteral("none"),
          "the balloon is spelled out as none");
    check(doc.elementsByTagName(QStringLiteral("vsock")).isEmpty(), "no vsock device");
    check(doc.elementsByTagName(QStringLiteral("iothreads")).isEmpty(), "no iothreads");
    const QString all_text = doc.toString();
    check(!all_text.contains(QStringLiteral("virtio-win"), Qt::CaseInsensitive)
              && !all_text.contains(QStringLiteral("bus='virtio'"))
              && !all_text.contains(QStringLiteral("bus=\"virtio\"")),
          "nothing of virtio-win and no virtio bus device");
}

QString video_model(const QDomDocument& doc) {
    const QDomNodeList videos = doc.elementsByTagName(QStringLiteral("video"));
    if (videos.count() != 1) {
        return QStringLiteral("<%1 video elements>").arg(videos.count());
    }
    return videos.at(0)
        .toElement()
        .firstChildElement(QStringLiteral("model"))
        .attribute(QStringLiteral("type"));
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    check(DomainXmlBuilder::xml_escape(QStringLiteral("a&b<c>d\"e'f")) ==
              QStringLiteral("a&amp;b&lt;c&gt;d&quot;e&apos;f"),
          "xml_escape covers all five XML specials");
    check(DomainXmlBuilder::xml_escape(QStringLiteral("/home/ünïcode/plain")) ==
              QStringLiteral("/home/ünïcode/plain"),
          "xml_escape leaves ordinary text alone");

    // One pass: a value that looks like a placeholder is not expanded.
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("a"), QStringLiteral("{{b}}"));
    vars.insert(QStringLiteral("b"), QStringLiteral("B"));
    check(DomainXmlBuilder::render_template(QStringLiteral("<{{a}}|{{b}}|{{c}}>"), vars) ==
              QStringLiteral("<{{b}}|B|{{c}}>"),
          "render_template substitutes once and leaves unknown placeholders");

    // End to end: a hostile share path and a hostile disk path survive
    // into a definition that parses and reads back as what was asked.
    VmRecord vm;
    vm.uuid = QUuid::createUuid();
    vm.name = QStringLiteral("lsw");
    vm.vsock_cid = 100;
    vm.disk_path = QStringLiteral("/home/o'brien/disks/lsw.qcow2");
    vm.nvram_path = QStringLiteral("/tmp/nvram.fd");
    vm.iso_path = QStringLiteral("/tmp/windows.iso");
    vm.install_media_path = QStringLiteral("/tmp/lsw-install.qcow2");
    vm.payload_iso_path = QStringLiteral("/tmp/payload.iso");
    DomainXmlInputs inputs;
    const QString hostile = QStringLiteral("/home/me/Tom's & Jerry's <stuff>");
    inputs.shares.push_back(DomainShare{QStringLiteral("z"), hostile, true});

    // The GPU, as the settings would have it, with paths a user could
    // have: an apostrophe for XML, a comma for QEMU's option parser.
    inputs.qemu_binary = QStringLiteral("/opt/o'av/qemu/qemu-system-x86_64");
    inputs.qemu_module_dir = QStringLiteral("/opt/o'av/qemu");
    inputs.qemu_data_dir = QStringLiteral("/opt/o'av/qemu/share");
    inputs.executor = QStringLiteral("/opt/a,b/libav_executor.so");
    inputs.d3d_lib_dir = QStringLiteral("/opt/av & d3d/lib");
    inputs.host_gpu_uuid = QStringLiteral("01234567-89ab-cdef-0123-456789abcdef");
    inputs.render_only_gpu_uuids = {QStringLiteral("fedcba98-7654-3210-fedc-ba9876543210")};
    inputs.host_window_mib = 4096;
    inputs.display_modes = QStringLiteral("2560x1440,1920x1080");
    inputs.avgpu_log = QStringLiteral("/home/o'brien/.local/share/lsw/logs/lsw-avgpu.log");
    inputs.export_socket = QStringLiteral("/run/user/1000/o'b,c-export.sock");

    const QString xml = DomainXmlBuilder::build(vm, inputs);
    const QDomDocument doc = parse(xml, "a definition with hostile paths is well-formed XML");

    const QDomElement fs = doc.elementsByTagName(QStringLiteral("filesystem")).at(0).toElement();
    check(fs.firstChildElement(QStringLiteral("source")).attribute(QStringLiteral("dir")) ==
              hostile,
          "the share path reads back exactly");
    check(!fs.firstChildElement(QStringLiteral("readonly")).isNull(),
          "a read-only share is marked readonly");

    bool disk_found = false;
    const QDomNodeList sources = doc.elementsByTagName(QStringLiteral("source"));
    for (int i = 0; i < sources.count(); ++i) {
        if (sources.at(i).toElement().attribute(QStringLiteral("file")) == vm.disk_path) {
            disk_found = true;
        }
    }
    check(disk_found, "the disk path with an apostrophe reads back exactly");
    check(!xml.contains(QStringLiteral("{{")), "no placeholder is left unsubstituted");

    // accel-virt's QEMU and its environment.
    check(doc.elementsByTagName(QStringLiteral("emulator")).at(0).toElement().text() ==
              inputs.qemu_binary,
          "the emulator is accel-virt's QEMU, read back exactly");
    check(qemu_env(doc, QStringLiteral("QEMU_MODULE_DIR")) == inputs.qemu_module_dir,
          "QEMU_MODULE_DIR is passed, read back exactly");
    check(qemu_env(doc, QStringLiteral("AV_D3D_PATH")) == inputs.d3d_lib_dir,
          "AV_D3D_PATH is passed, read back exactly");
    {
        const QStringList all = qemu_args(doc);
        check(all.count(QStringLiteral("-L")) == 1 &&
                  args_after(all, QStringLiteral("-L"), QString{}).value(0) ==
                      inputs.qemu_data_dir,
              "an installed QEMU's data directory is passed once as -L, read back exactly");
    }

    // The guest's RAM is a shared memfd.
    const QDomElement backing =
        doc.elementsByTagName(QStringLiteral("memoryBacking")).at(0).toElement();
    check(
        backing.firstChildElement(QStringLiteral("source")).attribute(QStringLiteral("type")) ==
                QStringLiteral("memfd") &&
            backing.firstChildElement(QStringLiteral("access")).attribute(QStringLiteral("mode")) ==
                QStringLiteral("shared"),
        "guest RAM is a shared memfd");

    check(video_model(doc) == QStringLiteral("none"), "steady state has no emulated adapter");

    // The GPU devices.
    const QStringList args = qemu_args(doc);
    const QStringList gpus =
        args_after(args, QStringLiteral("-device"), QStringLiteral("virtio-gpu-av-pci,"));
    check(gpus.size() == 2, "one display adapter and one render-only adapter");
    const QString display = gpus.value(0);
    check(display.startsWith(QStringLiteral("virtio-gpu-av-pci,id=lsw-gpu,bus=pcie.0,addr=0x11,")),
          "the display adapter has the stable id and its own PCI slot");
    check(DomainXmlBuilder::gpu_device_id() == QStringLiteral("lsw-gpu"),
          "the id the QMP path uses is the one the device has");
    check(display.contains(QStringLiteral(",host-gpu=01234567-89ab-cdef-0123-456789abcdef,")),
          "the display adapter is bound by UUID");
    check(display.contains(QStringLiteral(",executor=/opt/a,,b/libav_executor.so,")),
          "a comma in the executor path is doubled for QEMU");
    check(display.contains(QStringLiteral(",max_outputs=1,")), "one output");
    check(display.contains(QStringLiteral(",vectors=11,hostmem=4096M,")),
          "MSI-X vectors and the host window");
    // The executor runs inside QEMU: libvirt's seccomp sandbox
    // (spawn=deny) would not let QEMU start a renderer process.
    check(!xml.contains(QStringLiteral("renderer")), "no device is given a renderer");
    check(display.endsWith(QStringLiteral(",modes=2560x1440,,1920x1080")),
          "the mode list's commas are doubled for QEMU");
    check(!display.contains(QStringLiteral("display=off")), "the display adapter displays");
    check(display.contains(QStringLiteral(",export-socket=/run/user/1000/o'b,,c-export.sock,")),
          "the display adapter has the export socket, its comma doubled for QEMU");
    check(xml.count(QStringLiteral("export-socket=")) == 1, "and no other adapter has one");
    const QString render_only = gpus.value(1);
    check(render_only.startsWith(
              QStringLiteral("virtio-gpu-av-pci,id=lsw-gpu-1,bus=pcie.0,addr=0x12,")),
          "the render-only adapter takes the next slot");
    check(render_only.contains(QStringLiteral(",host-gpu=fedcba98-7654-3210-fedc-ba9876543210,")) &&
              render_only.contains(QStringLiteral(",display=off,")) &&
              render_only.contains(QStringLiteral(",hostmem=4096M")) &&
              !render_only.contains(QStringLiteral("modes=")),
          "the render-only adapter is display=off on its own GPU");

    // The driver's log.
    check(args_after(args, QStringLiteral("-chardev"), QStringLiteral("file,id=avgpu-log,path="))
                  .value(0) == QStringLiteral("file,id=avgpu-log,path=") + inputs.avgpu_log,
          "the driver log's chardev writes the log file");
    check(args_after(args, QStringLiteral("-device"), QStringLiteral("isa-debugcon,")).value(0) ==
              QStringLiteral("isa-debugcon,iobase=0x402,chardev=avgpu-log"),
          "the debug console listens on port 0x402");

    // The agent talks TCP over the user-net hostfwd, and the render
    // server's regions are gone: the domain has no shared memory device.
    check(doc.elementsByTagName(QStringLiteral("shmem")).isEmpty(),
          "the domain has no shmem device");
    check(!xml.contains(QStringLiteral("rvk")), "nothing of the render server is left");

    check_inbox_devices(doc);
    check(doc.elementsByTagName(QStringLiteral("graphics")).isEmpty(),
          "steady-state VMs have no VNC or desktop readback backend");

    // Windows setup: a VGA primary as well, the GPU device still there.
    DomainXmlInputs install = inputs;
    install.install_display = true;
    install.shares.clear();
    install.render_only_gpu_uuids.clear();
    install.display_modes.clear();
    install.host_gpu_uuid.clear();
    install.qemu_data_dir.clear();
    const QDomDocument install_doc =
        parse(DomainXmlBuilder::build(vm, install), "the install definition is well-formed XML");
    check(video_model(install_doc) == QStringLiteral("vga"),
          "the install definition has a VGA adapter");
    check(install_doc.elementsByTagName(QStringLiteral("graphics")).count() == 1 &&
              install_doc.elementsByTagName(QStringLiteral("graphics")).at(0).toElement()
                  .attribute(QStringLiteral("type")) == QStringLiteral("vnc"),
          "Windows setup keeps the plain VGA VNC console until WGC is available");
    check(install_doc.elementsByTagName(QStringLiteral("video"))
                  .at(0)
                  .firstChildElement(QStringLiteral("model"))
                  .attribute(QStringLiteral("primary")) == QStringLiteral("yes"),
          "the VGA is the primary display");
    const QStringList install_gpus = args_after(qemu_args(install_doc), QStringLiteral("-device"),
                                                QStringLiteral("virtio-gpu-av-pci,"));
    check(install_gpus.size() == 1, "the GPU device is present during setup");
    check(!install_gpus.value(0).contains(QStringLiteral("export-socket=")),
          "nothing is mirrored during setup, so there is no export socket");
    DomainXmlInputs unsocketed = inputs;
    unsocketed.export_socket.clear();
    check(!DomainXmlBuilder::build(vm, unsocketed).contains(QStringLiteral("export-socket")),
          "without a runtime directory there is no export socket");
    check(install_gpus.value(0).contains(QStringLiteral(",host-gpu=auto,")),
          "no UUID is host-gpu=auto");
    check(!install_gpus.value(0).contains(QStringLiteral("modes=")),
          "no modes leave the option out");
    check(install_gpus.value(0).contains(QStringLiteral(",max_outputs=0")) &&
              !install_gpus.value(0).contains(QStringLiteral("max_outputs=1")),
          "during setup the GPU has no outputs, so the VNC console is the VGA's");
    {
        DomainXmlInputs with_modes = install;
        with_modes.display_modes = QStringLiteral("1920x1080");
        const QStringList with_modes_gpus =
            args_after(qemu_args(parse(DomainXmlBuilder::build(vm, with_modes), "")),
                       QStringLiteral("-device"), QStringLiteral("virtio-gpu-av-pci,"));
        check(!with_modes_gpus.value(0).contains(QStringLiteral("modes=")),
              "a GPU with no outputs is given no modes even when some are set");
    }
    check(!qemu_args(install_doc).contains(QStringLiteral("-L")),
          "a build tree's QEMU, with no data directory, gets no -L");
    check(!install_doc.elementsByTagName(QStringLiteral("memoryBacking")).isEmpty(),
          "guest RAM is a shared memfd with no shares too");
    check_inbox_devices(install_doc);

    // The display settings are live; the GPU's are not.
    check(!ManagerSettings::needs_restart(ManagerSettings::Key::kDisplayModes) &&
              !ManagerSettings::needs_restart(ManagerSettings::Key::kMatchHostOutput),
          "the resolution applies without a restart");
    check(ManagerSettings::needs_restart(ManagerSettings::Key::kHostGpu) &&
              ManagerSettings::needs_restart(ManagerSettings::Key::kHostWindowMib) &&
              ManagerSettings::needs_restart(ManagerSettings::Key::kRenderGpus),
          "the GPU device's settings need a restart");
    check(ManagerSettings::default_value(ManagerSettings::Key::kHostGpu) ==
                  QStringLiteral("auto") &&
              ManagerSettings::default_value(ManagerSettings::Key::kHostWindowMib) == 8192 &&
              ManagerSettings::default_value(ManagerSettings::Key::kRenderGpus).toStringList()
                  .isEmpty(),
          "the GPU defaults are auto, no render GPUs and an 8 GiB window");

    // The stored form of the share list round-trips, read-only flag
    // and all, and an empty list is the empty string.
    const QVector<HostShare> shares = {
        HostShare{QStringLiteral("/home/me/a b"), QStringLiteral("Z:"), false},
        HostShare{QStringLiteral("/srv/ro"), QStringLiteral("Y:"), true},
    };
    const QString blob = ManagerSettings::encode_host_shares(shares);
    check(ManagerSettings::decode_host_shares(blob) == shares, "host shares round-trip");
    check(ManagerSettings::encode_host_shares({}).isEmpty(), "no shares encode as empty");
    check(ManagerSettings::decode_host_shares(QString{}).isEmpty(), "empty decodes as no shares");

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
