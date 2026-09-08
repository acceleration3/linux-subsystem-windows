// Regression test: the rendered answer file must be non-empty, valid
// XML, with every placeholder substituted.
//
// A silently-empty autounattend.xml is the worst failure mode this
// project has: the ISO builds, the VM boots, and Windows just shows the
// interactive installer — with nothing anywhere saying why.

#include "lsw/host/provision/install/autounattend.hpp"
#include "lsw/host/provision/install/ivshmem_driver.hpp"
#include "lsw/host/provision/install/payload_iso.hpp"

#include <QDomDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) {
        ++failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    using namespace lsw::host::provision::install;

    AutounattendInputs in;
    in.computer_name = QStringLiteral("LSW-TEST");
    in.user_name = QStringLiteral("tester");
    in.user_password = QStringLiteral("hunter2");
    in.image_index = 6;

    const QString xml = Autounattend::render(in);

    check(!xml.isEmpty(), "render() returns a non-empty document");
    check(xml.size() > 1000, "rendered document is a plausible size");

    // Every substitutable placeholder must be gone. Checked by name
    // rather than by scanning for "{{", because the template's own
    // comments talk about the placeholder syntax.
    bool any_left = false;
    for (const char* key : {"locale", "timezone", "computer_name", "user_name",
                            "user_password", "product_key", "image_index",
                            "win11_bypass_block"}) {
        if (xml.contains(QStringLiteral("{{%1}}").arg(QLatin1String(key)))) {
            std::printf("       unsubstituted: {{%s}}\n", key);
            any_left = true;
        }
    }
    check(!any_left, "no unsubstituted placeholders remain");

    // The inputs must actually reach the output.
    check(xml.contains(QStringLiteral("LSW-TEST")), "computer_name substituted");
    check(xml.contains(QStringLiteral("tester")), "user_name substituted");

    QDomDocument doc;
    const bool parsed = static_cast<bool>(doc.setContent(xml));
    check(parsed, "rendered document is well-formed XML");
    check(doc.documentElement().tagName() == QStringLiteral("unattend"),
          "root element is <unattend>");

    // A RunSynchronousCommand's <Path> is capped at 259 characters
    // (MAX_PATH). Going over does not fail that one command: setup
    // rejects the ENTIRE answer file with 0x80220005 and the install
    // dies at specialize behind "The computer restarted unexpectedly".
    // Two 1-GiB-VRAM registry writes once did exactly that, which is
    // why anything that long now lives in Specialize.ps1 instead.
    constexpr int kMaxPathLength = 259;
    bool paths_within_limit = true;
    const QDomNodeList commands = doc.elementsByTagName(QStringLiteral("RunSynchronousCommand"));
    for (int i = 0; i < commands.count(); ++i) {
        const QDomElement command = commands.at(i).toElement();
        const QString path = command.firstChildElement(QStringLiteral("Path")).text();
        if (path.size() > kMaxPathLength) {
            std::printf("       Order %s: <Path> is %d characters\n",
                        qPrintable(command.firstChildElement(QStringLiteral("Order")).text()),
                        static_cast<int>(path.size()));
            paths_within_limit = false;
        }
    }
    check(commands.count() > 0, "the answer file has RunSynchronousCommand entries");
    check(paths_within_limit, "every RunSynchronousCommand <Path> is within 259 characters");

    // And it must survive the trip onto the ISO staging directory —
    // this is what actually lands at the root of the payload ISO.
    QTemporaryDir staging;
    PayloadInputs payload;
    payload.autounattend_xml = xml;
    const auto staged = PayloadIsoBuilder::stage_files(payload, staging.path());
    check(!staged.isEmpty(), "stage_files() reports a staged answer file");
    const QString staged_path = QDir(staging.path()).filePath("autounattend.xml");
    check(QFile::exists(staged_path), "autounattend.xml exists in the staging dir");
    check(QFileInfo(staged_path).size() > 1000, "staged autounattend.xml is not empty");

    // The shared-memory device must be live on the FIRST boot: it is the
    // transport the agent talks over, so the driver cannot arrive as a
    // guest setup step. That means two things have to hold together —
    // the answer file has to ask for the injection, and the files have
    // to actually be on the ISO for it to find.
    check(xml.contains(QStringLiteral("\\ivshmem")),
          "autounattend injects the payload ISO's ivshmem driver directory");

    QTemporaryDir driver_src;
    for (const char* name : {"ivshmem.inf", "ivshmem.cat", "ivshmem.sys"}) {
        QFile f{QDir(driver_src.path()).filePath(QLatin1String(name))};
        check(f.open(QIODevice::WriteOnly), "created a fake driver file");
        f.write("stub");
    }
    check(IvshmemDriver::looks_like_driver_dir(driver_src.path()),
          "a dir with .inf/.cat/.sys is recognised as a driver");

    QTemporaryDir partial;
    {
        QFile f{QDir(partial.path()).filePath(QStringLiteral("ivshmem.inf"))};
        check(f.open(QIODevice::WriteOnly), "created a lone .inf");
        f.write("stub");
    }
    check(!IvshmemDriver::looks_like_driver_dir(partial.path()),
          "an .inf without its catalog and binary is refused — DISM would reject it");

    QTemporaryDir staging2;
    PayloadInputs with_driver;
    with_driver.autounattend_xml = xml;
    with_driver.ivshmem_driver_dir = driver_src.path();
    (void)PayloadIsoBuilder::stage_files(with_driver, staging2.path());
    for (const char* name : {"ivshmem.inf", "ivshmem.cat", "ivshmem.sys"}) {
        const QString on_iso =
            QDir(staging2.path()).filePath(QStringLiteral("ivshmem/") + QLatin1String(name));
        check(QFile::exists(on_iso), "driver file staged into \\ivshmem on the ISO");
    }

    // E2: the guest drivers. All four files per architecture have to
    // land in \\rvk (and \\rvk32) on the ISO under the names the guest
    // wants, and the answer file has to copy them in — nothing
    // registers them, because the agent points each launch at the set
    // it staged beside that application.
    check(xml.contains(QStringLiteral("C:\\ProgramData\\LSW\\rvk")),
          "autounattend copies the guest drivers to ProgramData");
    check(xml.contains(QStringLiteral("C:\\ProgramData\\LSW\\rvk32")),
          "and the 32-bit set, which is what a D3D8 / D3D9 title loads");

    QTemporaryDir drv_src;
    for (const char* name : {"librvk_icd.dll", "opengl32.dll", "librvk_opencl.dll",
                             "vulkan-1.dll"}) {
        QFile f{QDir(drv_src.path()).filePath(QLatin1String(name))};
        check(f.open(QIODevice::WriteOnly), "created a fake guest driver");
        f.write("MZstub");
    }
    const QString vk_icd = QDir(drv_src.path()).filePath(QStringLiteral("librvk_icd.dll"));
    const QString gl_icd = QDir(drv_src.path()).filePath(QStringLiteral("opengl32.dll"));
    const QString cl_icd = QDir(drv_src.path()).filePath(QStringLiteral("librvk_opencl.dll"));
    const QString loader = QDir(drv_src.path()).filePath(QStringLiteral("vulkan-1.dll"));

    QTemporaryDir staging3;
    PayloadInputs with_drivers;
    with_drivers.autounattend_xml = xml;
    with_drivers.vulkan_icd_path = vk_icd;
    with_drivers.opengl_icd_path = gl_icd;
    with_drivers.opencl_icd_path = cl_icd;
    with_drivers.vulkan_loader_path = loader;
    (void)PayloadIsoBuilder::stage_files(with_drivers, staging3.path());
    // The names on the ISO are the guest's names, not the cross-build's:
    // the mingw ``lib`` prefix means nothing to Windows, and the agent's
    // manifest points at rvk_icd.dll.
    for (const char* name : {"rvk_icd.dll", "opengl32.dll", "rvk_opencl.dll", "vulkan-1.dll"}) {
        const QString on_iso =
            QDir(staging3.path()).filePath(QStringLiteral("rvk/") + QLatin1String(name));
        check(QFile::exists(on_iso), "guest driver staged into \\rvk on the ISO");
    }

    // The i686 set stages into its own directory. It has to be a
    // separate one rather than a suffix: opengl32.dll is found by the
    // DLL search order, so two builds in one folder would shadow each
    // other.
    QTemporaryDir staging4;
    PayloadInputs with_drivers32 = with_drivers;
    with_drivers32.vulkan_icd32_path = vk_icd;
    with_drivers32.opengl_icd32_path = gl_icd;
    with_drivers32.opencl_icd32_path = cl_icd;
    with_drivers32.vulkan32_loader_path = loader;
    (void)PayloadIsoBuilder::stage_files(with_drivers32, staging4.path());
    for (const char* name : {"rvk_icd.dll", "opengl32.dll", "rvk_opencl.dll", "vulkan-1.dll"}) {
        const QString on_iso =
            QDir(staging4.path()).filePath(QStringLiteral("rvk32/") + QLatin1String(name));
        check(QFile::exists(on_iso), "32-bit guest driver staged into \\rvk32");
    }

    // End to end: build a real ISO and read the answer file back out of
    // it. This is the step that actually failed in the field — the file
    // was present at the ISO root but zero bytes, so Windows Setup
    // silently fell through to the interactive installer.
    QTemporaryDir iso_dir;
    PayloadInputs iso_inputs;
    iso_inputs.autounattend_xml = xml;
    iso_inputs.output_iso_path = QDir(iso_dir.path()).filePath("payload.iso");
    // Optional: stage a real agent binary too, so the test exercises the
    // same multi-file staging the wizard does rather than a lone XML.
    if (argc > 1) {
        iso_inputs.agent_exe_path = QString::fromLocal8Bit(argv[1]);
        std::printf("       also staging: %s\n", argv[1]);
    }
    const int rc = PayloadIsoBuilder::build(iso_inputs);
    check(rc == 0, "PayloadIsoBuilder::build() succeeds");
    check(QFileInfo(iso_inputs.output_iso_path).size() > 0, "ISO was written");

    // Look for the answer file's CONTENT in the image, not just its
    // name: a zero-length entry still has a name. The substituted
    // computer name only exists if the rendered bytes were written.
    QFile iso{iso_inputs.output_iso_path};
    bool found_non_empty = false;
    if (iso.open(QIODevice::ReadOnly)) {
        const QByteArray raw = iso.readAll();
        check(raw.contains("autounattend.xml"), "autounattend.xml present in the ISO");
        found_non_empty = raw.contains("LSW-TEST") && raw.contains("<unattend");
    }
    check(found_non_empty, "autounattend.xml on the ISO is not empty");

    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
