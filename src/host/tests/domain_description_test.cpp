#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/domain_xml_builder.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QCoreApplication>
#include <QTemporaryFile>
#include <QUuid>
#include <cstdio>

using lsw::host::config::VmRecord;
using lsw::host::vm::DiskImage;
using lsw::host::vm::DomainDescription;
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

const char* const kLiveXml = R"(<domain type='kvm' id='3' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <name>lsw</name>
  <memory unit='KiB'>16777216</memory>
  <currentMemory unit='KiB'>16777216</currentMemory>
  <vcpu placement='static'>12</vcpu>
  <devices>
    <disk type='file' device='cdrom'>
      <driver name='qemu' type='raw'/>
      <target dev='sdb' bus='sata'/>
    </disk>
    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2' cache='none' io='native' discard='unmap'/>
      <source file='/home/u/.local/share/lsw/disks/lsw.qcow2' index='2'/>
      <target dev='nvme0n1' bus='nvme'/>
      <serial>LSW-SYSTEM</serial>
      <address type='drive' controller='0' bus='0' target='0' unit='0'/>
    </disk>
  </devices>
  <qemu:commandline>
    <qemu:arg value='-device'/>
    <qemu:arg value='virtio-gpu-av-pci,id=lsw-gpu,bus=pcie.0,addr=0x11,host-gpu=auto,max_outputs=1,vectors=11,hostmem=16384M,vram-reserve=1024,modes=2560x1440,,1920x1080'/>
    <qemu:arg value='-device'/>
    <qemu:arg value='virtio-gpu-av-pci,id=lsw-gpu-1,bus=pcie.0,addr=0x12,host-gpu=fedcba98-7654-3210-fedc-ba9876543210,display=off,vectors=11,hostmem=16384M'/>
    <qemu:arg value='-netdev'/>
    <qemu:arg value='user,id=lsw-net'/>
  </qemu:commandline>
</domain>)";

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    check(DomainDescription::split_options(QStringLiteral("a,id=x,modes=1x1,,2x2,last")) ==
              QStringList({QStringLiteral("a"), QStringLiteral("id=x"),
                           QStringLiteral("modes=1x1,2x2"), QStringLiteral("last")}),
          "split_options splits at single commas and reads ,, as one");
    check(DomainDescription::split_options(QStringLiteral("path=/a,,,,b")) ==
              QStringList({QStringLiteral("path=/a,,b")}),
          "split_options keeps two doubled commas as two commas");
    check(DomainDescription::size_mib(QStringLiteral("8192M")) == 8192, "8192M is 8192 MiB");
    check(DomainDescription::size_mib(QStringLiteral("16G")) == 16384, "16G is 16384 MiB");
    check(DomainDescription::size_mib(QStringLiteral("1073741824")) == 1024,
          "a bare number is bytes");
    check(DomainDescription::size_mib(QStringLiteral("lots")) == 0, "nonsense is 0");

    VmRecord vm;
    vm.uuid = QUuid::createUuid();
    vm.name = QStringLiteral("lsw");
    vm.disk_path = QStringLiteral("/tmp/a,b/lsw.qcow2");
    vm.sizing.cpu_cores = 6;
    vm.sizing.memory_mib = 12288;
    DomainXmlInputs inputs;
    inputs.qemu_binary = QStringLiteral("/opt/av/qemu-system-x86_64");
    inputs.executor = QStringLiteral("/opt/a,b/libav_executor.so");
    inputs.host_gpu_uuid = QStringLiteral("01234567-89ab-cdef-0123-456789abcdef");
    inputs.render_only_gpu_uuids = {QStringLiteral("fedcba98-7654-3210-fedc-ba9876543210"),
                                    QStringLiteral("11111111-2222-3333-4444-555555555555")};
    inputs.host_window_mib = 4096;
    inputs.vram_reserve_mib = 2048;
    inputs.display_modes = QStringLiteral("2560x1440,1920x1080");
    inputs.display_refresh_millihz = 359980;
    const auto built = DomainDescription::parse(DomainXmlBuilder::build(vm, inputs));
    check(built.has_value(), "the builder's definition parses");
    if (built) {
        check(built->vcpus == 6, "vcpus read back");
        check(built->memory_mib == 12288, "memory read back in MiB");
        check(built->disk_path == vm.disk_path, "the system disk path reads back");
        check(built->disk_bus == QStringLiteral("nvme"), "the system disk is NVMe");
        check(built->gpus.size() == 3, "one display and two render-only devices");
        const auto* display = built->display();
        check(display != nullptr && display->id == QStringLiteral("lsw-gpu"),
              "the display device is the first, by its id");
        if (display != nullptr) {
            check(display->host_gpu == inputs.host_gpu_uuid, "the display's host-gpu");
            check(display->hostmem_mib == 4096, "the display's host window");
            check(display->vram_reserve_mib == 2048, "the display's VRAM reserve");
            check(display->modes == inputs.display_modes, "the display's modes, commas restored");
            check(display->refresh_millihz == 359980u, "the display's refresh, in millihertz");
        }
        check(built->gpus.size() == 3 && !built->gpus.at(1).display && !built->gpus.at(2).display,
              "the render-only devices are display=off");
        check(built->gpus.size() == 3
                  && built->gpus.at(2).host_gpu == inputs.render_only_gpu_uuids.at(1),
              "each render-only device keeps its own GPU");
        check(!built->installing, "the steady definition is a guest whose setup has finished");
    }
    {
        DomainXmlInputs install = inputs;
        install.install_display = true;
        const auto during = DomainDescription::parse(DomainXmlBuilder::build(vm, install));
        check(during && during->installing, "the install-time one, with its VGA, is not");
    }

    inputs.host_gpu_uuid.clear();
    inputs.render_only_gpu_uuids.clear();
    inputs.display_modes.clear();
    inputs.display_refresh_millihz = 0;
    const auto plain = DomainDescription::parse(DomainXmlBuilder::build(vm, inputs));
    check(plain && plain->gpus.size() == 1 && plain->gpus.first().host_gpu == QStringLiteral("auto")
              && !plain->gpus.first().modes.has_value() && !plain->gpus.first().refresh_millihz.has_value(),
          "auto, no modes, no refresh: read back as such");

    const auto live = DomainDescription::parse(QString::fromUtf8(kLiveXml));
    check(live.has_value(), "libvirt's live XML parses");
    if (live) {
        check(live->vcpus == 12 && live->memory_mib == 16384, "12 vcpus, 16 GiB from KiB");
        check(live->disk_path.endsWith(QStringLiteral("lsw.qcow2")),
              "the cdrom before the disk is not taken for it");
        check(live->disk_bus == QStringLiteral("nvme"), "the live disk reads as NVMe");
        check(live->gpus.size() == 2 && live->display() != nullptr
                  && live->display()->hostmem_mib == 16384
                  && live->display()->vram_reserve_mib == 1024
                  && live->gpus.at(1).vram_reserve_mib == 0
                  && live->display()->modes == QStringLiteral("2560x1440,1920x1080"),
              "the live GPU lines read back");
        check(live->gpus.size() == 2 && !live->gpus.at(1).display, "display=off read");
    }
    check(live && live->usb_devices.isEmpty(), "a definition with no hostdev has no USB devices");

    const auto with_usb = DomainDescription::parse(QStringLiteral(
        "<domain><devices>"
        "<hostdev mode='subsystem' type='usb' managed='no'><source>"
        "<vendor id='0x056a'/><product id='0x0374'/><address bus='3' device='12'/></source>"
        "<address type='usb' bus='0' port='1'/></hostdev>"
        "<hostdev mode='subsystem' type='pci' managed='yes'><source>"
        "<address domain='0x0000' bus='0x01' slot='0x00' function='0x0'/></source></hostdev>"
        "<hostdev mode='subsystem' type='usb'><source>"
        "<vendor id='0x1050'/><product id='0x0407'/><address bus='1' device='4'/></source>"
        "</hostdev></devices></domain>"));
    check(with_usb && with_usb->usb_devices.size() == 2, "two USB hostdevs, the PCI one not counted");
    if (with_usb && with_usb->usb_devices.size() == 2) {
        const auto& first = with_usb->usb_devices.at(0);
        check(first.vendor == 0x056a && first.product == 0x0374 && first.bus == 3
                  && first.device == 12,
              "a USB hostdev's vendor, product, bus and device read back");
        check(with_usb->usb_devices.at(1).vendor == 0x1050 && with_usb->usb_devices.at(1).device == 4,
              "the second USB hostdev reads back");
    }
    check(lsw::host::vm::Lifecycle::usb_hostdev_xml({0x56a, 0x374, 3, 12}) ==
              QStringLiteral("<hostdev mode='subsystem' type='usb'><source>"
                             "<vendor id='0x056a'/><product id='0x0374'/>"
                             "<address bus='3' device='12'/></source></hostdev>"),
          "the attach XML names the device by vendor, product and bus address");
    check(DomainDescription::parse(lsw::host::vm::Lifecycle::usb_hostdev_xml({0x1, 0x2, 1, 2})
                                   .prepend(QStringLiteral("<domain><devices>"))
                                   .append(QStringLiteral("</devices></domain>")))
              ->usb_devices.size() == 1,
          "the attach XML is what the description reads");
    check(!DomainDescription::parse(QStringLiteral("<network/>")).has_value(),
          "something that is not a domain is refused");
    check(!DomainDescription::parse(QStringLiteral("<domain><vcpu>")).has_value(),
          "truncated XML is refused");

    QByteArray header(32, '\0');
    header.replace(0, 4, QByteArrayLiteral("QFI\xfb"));
    const quint64 sixty_gib = 60ULL << 30;
    for (int i = 0; i < 8; ++i) {
        header[24 + i] = static_cast<char>((sixty_gib >> (56 - 8 * i)) & 0xff);
    }
    check(DiskImage::qcow2_virtual_size(header) == sixty_gib, "the qcow2 header's virtual size");
    check(!DiskImage::qcow2_virtual_size(QByteArray(32, 'x')).has_value(),
          "a file that is not qcow2 has no qcow2 size");
    QTemporaryFile file;
    if (file.open()) {
        file.write(header);
        file.flush();
        check(DiskImage::virtual_size(file.fileName()) == sixty_gib,
              "virtual_size reads a qcow2 file's header, not its length");
    }
    check(!DiskImage::virtual_size(QStringLiteral("/nonexistent/lsw.qcow2")).has_value(),
          "a missing image has no size");

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
