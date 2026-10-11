// What the guest's adapters are called. provision.ps1 names each GPU's
// INF model from C:\av\adapters.txt, which the payload now writes on the
// user's machine rather than shipping the one the driver package was
// built with; this pins that file to the rules accel-virt's device and
// its list_gpus.py use, against a fake sysfs and fake ID databases.
// No GPU, no Vulkan.

#include "lsw/host/provision/install/adapter_names.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

using lsw::host::platform::HostGpu;
using lsw::host::provision::install::AdapterNames;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void write(const QString& path, const QByteArray& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f{path};
    if (!f.open(QIODevice::WriteOnly) || f.write(text) != text.size()) {
        std::printf("FAIL could not write %s\n", qPrintable(path));
        ++g_failures;
    }
}

// A PCI function in the fake sysfs, as the kernel writes its attributes.
void pci_function(const QDir& sysfs, const char* address, const char* vendor, const char* device,
                  const char* revision) {
    const QString dir = sysfs.filePath(QLatin1String(address));
    write(dir + QStringLiteral("/vendor"), QByteArray(vendor) + '\n');
    write(dir + QStringLiteral("/device"), QByteArray(device) + '\n');
    write(dir + QStringLiteral("/revision"), QByteArray(revision) + '\n');
}

HostGpu gpu(const char* name, const char* pci_address, bool integrated = false,
            bool cpu = false) {
    HostGpu out;
    out.name = QString::fromUtf8(name);
    out.pci_address = QString::fromLatin1(pci_address);
    out.integrated = integrated;
    out.cpu = cpu;
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    QTemporaryDir root;
    const QDir dir{root.path()};
    AdapterNames::Sources sources;
    sources.sysfs_pci = dir.filePath(QStringLiteral("sys"));
    sources.amdgpu_ids = dir.filePath(QStringLiteral("amdgpu.ids"));
    sources.pci_ids = {dir.filePath(QStringLiteral("missing.ids")),
                       dir.filePath(QStringLiteral("pci.ids"))};

    const QDir sysfs{sources.sysfs_pci};
    pci_function(sysfs, "0000:03:00.0", "0x1002", "0x7550", "0xc0");  // discrete AMD
    pci_function(sysfs, "0000:10:00.0", "0x1002", "0x13c0", "0xc1");  // integrated AMD
    pci_function(sysfs, "0000:00:02.0", "0x8086", "0x7d67", "0x06");  // integrated Intel
    pci_function(sysfs, "0000:00:02.1", "0x8086", "0x46a6", "0x0c");  // integrated Intel
    pci_function(sysfs, "0000:01:00.0", "0x10de", "0x2684", "0xa1");  // discrete NVIDIA
    write(sources.amdgpu_ids, "# List of AMDGPU IDs\n"
                              "#\n"
                              "1.0.0\n"
                              "7550,\tC0,\tAMD Radeon RX 9070 XT\n"
                              "7550,\tC3,\tAMD Radeon RX 9070 GRE\n");
    write(dir.filePath(QStringLiteral("pci.ids")),
          "# pci.ids\n"
          "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n"
          "\t13c0  Granite Ridge [Radeon 610M]\n"
          "\t7550  Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]\n"
          "8086  Intel Corporation\n"
          "\t46a6  Alder Lake-P GT2 [Iris Xe Graphics]\n"
          "\t7d67  Arrow Lake-S [Intel Graphics]\n"
          "10de  NVIDIA Corporation\n"
          "\t2684  AD102 [GeForce RTX 4090]\n");

    const AdapterNames names{sources};

    check(names.name(gpu("AMD Radeon RX 9070 XT (RADV GFX1201)", "0000:03:00.0")) ==
              QStringLiteral("AMD Radeon RX 9070 XT"),
          "an AMD GPU takes libdrm's product name by device and revision");
    check(names.name(gpu("AMD Ryzen 9 9950X3D 16-Core Processor (RADV RAPHAEL_MENDOCINO)",
                         "0000:10:00.0", true)) == QStringLiteral("AMD Radeon(TM) Graphics"),
          "an integrated AMD GPU libdrm does not list is AMD's generic name, not pci.ids'");
    check(names.name(gpu("Intel(R) Graphics (ARL)", "0000:00:02.0", true)) ==
              QStringLiteral("Intel(R) Graphics"),
          "an integrated Intel GPU takes pci.ids' name after the vendor's, not repeating it");
    check(names.name(gpu("Intel(R) Iris(R) Xe Graphics (ADL GT2)", "0000:00:02.1", true)) ==
              QStringLiteral("Intel(R) Iris Xe Graphics"),
          "a pci.ids name without the vendor gets the vendor's prefix");
    check(names.name(gpu("NVIDIA GeForce RTX 4090", "0000:01:00.0")) ==
              QStringLiteral("NVIDIA GeForce RTX 4090"),
          "a discrete GPU outside AMD's table keeps its Vulkan name");
    check(names.name(gpu("Some GPU (driver 1.0)", "")) == QStringLiteral("Some GPU"),
          "the driver's parenthesised suffix is dropped");

    const QString long_name =
        QStringLiteral("A very long adapter name that runs well past ü forty seven bytes");
    const QString cut = names.name(gpu(long_name.toUtf8().constData(), ""));
    check(cut.toUtf8().size() <= 47 && long_name.startsWith(cut),
          "a long name is cut to the capset's 47 bytes, at a character boundary");

    const QString file =
        names.render({gpu("llvmpipe (LLVM 20.1.0, 256 bits)", "", false, true),
                      gpu("AMD Radeon RX 9070 XT (RADV GFX1201)", "0000:03:00.0"),
                      gpu("Virtual GPU", "")});
    check(file == QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\n"
                                 "1af4\t1050\tVirtual GPU\n"),
          "one line per GPU, IDs in lowercase hex from sysfs, CPU devices left out, "
          "virtio-gpu's IDs where there is no PCI function");
    check(names.render({gpu("llvmpipe", "", false, true)}).isEmpty(),
          "no GPU but a CPU device is an empty file");

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
