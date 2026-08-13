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

    // E2: the guest Vulkan pair. Both files plus a manifest have to land
    // in \vulkan on the ISO, and the answer file has to both copy them
    // in and register the ICD — a driver nothing registers is a driver
    // the loader never looks at, and the failure mode is "the app falls
    // back to software rendering" rather than anything that says so.
    check(xml.contains(QStringLiteral("Khronos\\Vulkan\\Drivers")),
          "autounattend registers the ICD in the Vulkan drivers key");
    check(xml.contains(QStringLiteral("lsw_icd.json")),
          "and the registered value names the manifest");

    QTemporaryDir vk_src;
    for (const char* name : {"lsw_icd.dll", "vulkan-1.dll"}) {
        QFile f{QDir(vk_src.path()).filePath(QLatin1String(name))};
        check(f.open(QIODevice::WriteOnly), "created a fake guest Vulkan binary");
        f.write("MZstub");
    }

    QTemporaryDir staging3;
    PayloadInputs with_vulkan;
    with_vulkan.autounattend_xml = xml;
    with_vulkan.icd_dll_path = QDir(vk_src.path()).filePath(QStringLiteral("lsw_icd.dll"));
    with_vulkan.vulkan_loader_path = QDir(vk_src.path()).filePath(QStringLiteral("vulkan-1.dll"));
    with_vulkan.icd_manifest_json = QStringLiteral("{\"ICD\":{\"library_path\":\".\\\\lsw_icd.dll\"}}");
    (void)PayloadIsoBuilder::stage_files(with_vulkan, staging3.path());
    for (const char* name : {"lsw_icd.dll", "vulkan-1.dll", "lsw_icd.json"}) {
        const QString on_iso =
            QDir(staging3.path()).filePath(QStringLiteral("vulkan/") + QLatin1String(name));
        check(QFile::exists(on_iso), "guest Vulkan file staged into \\vulkan on the ISO");
    }
    // The manifest's library_path must stay relative: the loader
    // resolves it against the manifest's own directory, so a relative
    // path survives being copied anywhere in the guest, and an absolute
    // one would duplicate a guest path the host has to guess.
    {
        QFile mf{QDir(staging3.path()).filePath(QStringLiteral("vulkan/lsw_icd.json"))};
        check(mf.open(QIODevice::ReadOnly), "staged manifest is readable");
        const QString body = QString::fromUtf8(mf.readAll());
        check(!body.contains(QStringLiteral("C:\\")),
              "the ICD manifest names the driver relatively, not by an absolute guest path");
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
