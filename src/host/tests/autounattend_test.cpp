// Regression test: the rendered answer file must be non-empty, valid
// XML, with every placeholder substituted.
//
// A silently-empty autounattend.xml is the worst failure mode this
// project has: the ISO builds, the VM boots, and Windows just shows the
// interactive installer — with nothing anywhere saying why.

#include "lsw/host/provision/install/autounattend.hpp"
#include "lsw/host/provision/install/payload_iso.hpp"
#include "lsw/host/provision/install/product_keys.hpp"
#include "lsw/host/provision/install/wim_edition_lister.hpp"
#include "lsw/host/services/guest_bundle.hpp"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) {
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace lsw::host::provision::install;

    AutounattendInputs in;
    in.computer_name = QStringLiteral("LSW-TEST");
    in.user_name = QStringLiteral("tester");
    in.user_password = QStringLiteral("hunter2");
    in.image_index = 6;
    in.product_key = ProductKeys::for_edition(QStringLiteral("Core"));
    in.release = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    in.version = QStringLiteral("9.8.7");

    const QString xml = Autounattend::render(in);

    check(!xml.isEmpty(), "render() returns a non-empty document");
    check(xml.size() > 1000, "rendered document is a plausible size");

    // Every substitutable placeholder must be gone. Checked by name
    // rather than by scanning for "{{", because the template's own
    // comments talk about the placeholder syntax.
    bool any_left = false;
    for (const char* key : {"locale", "timezone", "computer_name", "user_name", "user_password",
                            "product_key_block", "image_index", "win11_bypass_block", "release",
                            "version"}) {
        if (xml.contains(QStringLiteral("{{%1}}").arg(QLatin1String(key)))) {
            std::printf("       unsubstituted: {{%s}}\n", key);
            any_left = true;
        }
    }
    check(!any_left, "no unsubstituted placeholders remain");

    // The inputs must actually reach the output.
    check(xml.contains(QStringLiteral("LSW-TEST")), "computer_name substituted");
    {
        AutounattendInputs lisbon = in;
        lisbon.timezone = QStringLiteral("GMT Standard Time");
        check(Autounattend::render(lisbon).contains(
                  QStringLiteral("<TimeZone>GMT Standard Time</TimeZone>")),
              "the time zone reaches specialize, so the RTC's local time is read as the host's");
    }
    check(xml.contains(QStringLiteral("<Key>YTMG3-N6DKC-DKB77-7M9GH-8HVX7</Key>")),
          "a Home image gets the Home key, not Pro's");
    {
        AutounattendInputs keyless = in;
        keyless.product_key.clear();
        const QString bare = Autounattend::render(keyless);
        check(!bare.contains(QStringLiteral("<ProductKey>")) &&
                  bare.contains(QStringLiteral("<AcceptEula>true</AcceptEula>")),
              "no key leaves <ProductKey> out and keeps the rest of <UserData>");
    }
    check(ProductKeys::for_edition(QStringLiteral("professional")) ==
              QStringLiteral("W269N-WFGWX-YVC9B-4J6C9-T83GX"),
          "editions match whatever their case");
    check(ProductKeys::fallback() == ProductKeys::for_edition(QStringLiteral("Professional")),
          "an edition never read gets Pro's key");
    check(ProductKeys::for_edition(QStringLiteral("ServerDatacenter")).isEmpty() &&
              ProductKeys::for_edition(QString{}).isEmpty(),
          "an edition the table does not know gets no key");
    {
        // Microsoft's Windows 11 media: Home is image 1, Pro image 6.
        const QVector<WimEdition> media{{1, QStringLiteral("Windows 11 Home"), QStringLiteral("Core")},
                                        {2, QStringLiteral("Windows 11 Home N"), QStringLiteral("CoreN")},
                                        {3, QStringLiteral("Windows 11 Pro N"), QStringLiteral("ProfessionalN")},
                                        {4, QStringLiteral("Windows 11 Pro"), QStringLiteral("Professional")}};
        check(WimEditionLister::preferred(media) == 3, "Pro is offered first, not image 1");
        check(WimEditionLister::preferred({{1, QStringLiteral("Windows 11 Home"), QStringLiteral("Core")}}) == 0,
              "media without Pro offers its first image");
        check(WimEditionLister::preferred({{1, QStringLiteral("Windows 11 Home"), {}},
                                           {2, QStringLiteral("Windows 11 Pro"), {}}}) == 1,
              "with no EDITIONID the name decides");
        check(WimEditionLister::preferred({}) == 0, "an empty list gives 0");
    }
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
    // The release the ISO carries is required: a guest without it has no desktop.
    auto stub_bundle = std::make_shared<lsw::host::services::GuestBundle::Contents>();
    stub_bundle->files.push_back({"C:\\ProgramData\\LSW\\lsw-agent.exe", "agent", "hash"});
    payload.bundle = stub_bundle;
    QStringList staged;
    check(PayloadIsoBuilder::stage_files(payload, staging.path(), &staged, nullptr),
          "stage_files() succeeds with an answer file and a bundle");
    check(!staged.isEmpty(), "stage_files() reports a staged answer file");
    const QString staged_path = QDir(staging.path()).filePath("autounattend.xml");
    check(QFile::exists(staged_path), "autounattend.xml exists in the staging dir");
    check(QFileInfo(staged_path).size() > 1000, "staged autounattend.xml is not empty");

    // No driver injection: the disk (NVMe) and the NIC (e1000e) have
    // in-box drivers, so neither Setup nor the offline image is handed a
    // driver path, and nothing points at a virtio-win CD.
    check(!xml.contains(QStringLiteral("PnpCustomizations")) &&
              !xml.contains(QStringLiteral("DriverPaths")) &&
              !xml.contains(QStringLiteral("offlineServicing")),
          "the answer file injects no drivers");
    check(!xml.contains(QStringLiteral("viostor"), Qt::CaseInsensitive) &&
              !xml.contains(QStringLiteral("NetKVM"), Qt::CaseInsensitive),
          "nothing of virtio-win is left");

    // LSW's files. Setup copies nothing of them: SetupComplete finds the
    // payload volume, stages its \update under the release and runs the
    // update script from there, which signs and installs the driver, puts
    // the loaders, the shares, the service and the firewall rules in place.
    check(!xml.contains(QStringLiteral("rvk")), "nothing of the render server's drivers is left");
    check(!xml.contains(QStringLiteral("LswAvSign")) && !xml.contains(QStringLiteral("pristine")) &&
              !xml.contains(QStringLiteral("xcopy /E /I /Y %i:")) &&
              !xml.contains(QStringLiteral("firewall add rule name=\"LSW Agent")) &&
              !xml.contains(QStringLiteral("firewall add rule name=\"LSW Stream")) &&
              !xml.contains(QStringLiteral("av-installed.flag")) &&
              !xml.contains(QStringLiteral("lsw-av-install")),
          "specialize copies, signs and opens nothing of LSW's any more");
    check(xml.contains(QStringLiteral("md C:\\ProgramData\\LSW")) &&
              xml.contains(QStringLiteral("icacls C:\\ProgramData\\LSW /grant")),
          "specialize still makes C:\\ProgramData\\LSW with its ACL");
    check(xml.contains(QStringLiteral("$releaseId = '0123456789abcdef0123456789abcdef"
                                      "0123456789abcdef0123456789abcdef'")) &&
              xml.contains(QStringLiteral("$bundleVersion = '9.8.7'")),
          "the release and version reach SetupComplete");
    {
        const qsizetype volume = xml.indexOf(QStringLiteral("$_.VolumeLabel -eq 'LSW_PAYLOAD'"));
        const qsizetype stage = xml.indexOf(QStringLiteral("'C:\\ProgramData\\LSW\\update\\' + $releaseId.Substring(0, 16)"));
        const qsizetype run = xml.indexOf(QStringLiteral("-File $updateScript -Stage $stageDir -Release $releaseId "
                                                         "-Version $bundleVersion -NoRestart"));
        const qsizetype promote = xml.indexOf(QStringLiteral("promoted.flag present - skipping"));
        check(volume >= 0 && stage > volume && run > stage && promote > run,
              "SetupComplete finds the payload, stages its \\update and runs the update script "
              "with -NoRestart before it promotes the user");
    }
    check(xml.contains(QStringLiteral("DontSearchWindowsUpdate")),
          "driver searches on Windows Update are off before OOBE has the network");
    check(xml.contains(QStringLiteral("HKU\\DefaultUser\\Control Panel\\Desktop\" /v ScreenSaveActive")),
          "the screen saver is off for new accounts, since setup steps no longer run as the user");

    // The bundle stages as \update, each file under its guest path without
    // the drive's colon, beside the manifest and the mount list.
    using lsw::host::services::GuestBundle;
    auto bundle = std::make_shared<GuestBundle::Contents>();
    for (const char* guest_path : {"C:\\ProgramData\\LSW\\lsw-agent.exe", "C:\\av\\provision.ps1",
                                   "C:\\av\\package\\avgpu.inf", "C:\\av\\loaders\\x64\\vulkan-1.dll"}) {
        bundle->files.push_back({guest_path, QByteArray("stub for ") + guest_path, {}});
    }
    bundle->manifest = "manifest text\n";
    bundle->shares = "none";

    QTemporaryDir staging3;
    PayloadInputs with_bundle;
    with_bundle.autounattend_xml = xml;
    with_bundle.bundle = bundle;
    QString bundle_error;
    check(PayloadIsoBuilder::stage_files(with_bundle, staging3.path(), nullptr, &bundle_error),
          "stage_files() succeeds with the bundle");
    if (!bundle_error.isEmpty()) {
        std::printf("       %s\n", qPrintable(bundle_error));
    }
    for (const char* name : {"update/C/ProgramData/LSW/lsw-agent.exe", "update/C/av/provision.ps1",
                             "update/C/av/package/avgpu.inf", "update/C/av/loaders/x64/vulkan-1.dll",
                             "update/manifest.txt", "update/shares.txt"}) {
        check(QFile::exists(QDir(staging3.path()).filePath(QLatin1String(name))),
              "bundle file staged under \\update, where the update script reads it");
    }
    {
        QFile staged_agent{QDir(staging3.path()).filePath(QStringLiteral("update/C/ProgramData/LSW/lsw-agent.exe"))};
        check(staged_agent.open(QIODevice::ReadOnly) &&
                  staged_agent.readAll() == bundle->files.at(0).bytes,
              "a staged file is the bundle's bytes");
    }
    check(!QDir(staging3.path()).exists(QStringLiteral("av")) &&
              !QFile::exists(QDir(staging3.path()).filePath(QStringLiteral("lsw-agent.exe"))),
          "nothing of the bundle is on the ISO anywhere else");

    // No bundle, or one that could not be made, is refused, not half-staged:
    // the guest would have no display after a full install.
    {
        QTemporaryDir staging4;
        PayloadInputs none;
        none.autounattend_xml = xml;
        check(!PayloadIsoBuilder::stage_files(none, staging4.path(), nullptr, nullptr),
              "a payload with no bundle is refused");
        auto failed = std::make_shared<GuestBundle::Contents>();
        failed->error = QStringLiteral("no driver package");
        PayloadInputs bad = none;
        bad.bundle = failed;
        QString bad_error;
        check(!PayloadIsoBuilder::stage_files(bad, staging4.path(), nullptr, &bad_error) &&
                  bad_error.contains(QStringLiteral("no driver package")),
              "and so is one that could not be made, with its reason");
    }

    // End to end: build a real ISO and read the answer file back out of
    // it. This is the step that actually failed in the field — the file
    // was present at the ISO root but zero bytes, so Windows Setup
    // silently fell through to the interactive installer.
    QTemporaryDir iso_dir;
    PayloadInputs iso_inputs;
    iso_inputs.autounattend_xml = xml;
    iso_inputs.output_iso_path = QDir(iso_dir.path()).filePath("payload.iso");
    // The bundle too: its files are the nested tree on the ISO.
    iso_inputs.bundle = bundle;
    QString build_error;
    const bool built = PayloadIsoBuilder::build(iso_inputs, &build_error);
    if (!built) {
        std::printf("       %s\n", qPrintable(build_error));
    }
    check(built, "PayloadIsoBuilder::build() succeeds");
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
    {
        QFile again{iso_inputs.output_iso_path};
        const QByteArray raw = again.open(QIODevice::ReadOnly) ? again.readAll() : QByteArray{};
        check(raw.contains("stub for C:\\av\\package\\avgpu.inf"),
              "the bundle's nested files are in the ISO");
    }

    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
