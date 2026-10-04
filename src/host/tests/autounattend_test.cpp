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

#include <QDir>
#include <QDomDocument>
#include <QFile>
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
    in.product_key = ProductKeys::for_edition(QStringLiteral("Core"));

    const QString xml = Autounattend::render(in);

    check(!xml.isEmpty(), "render() returns a non-empty document");
    check(xml.size() > 1000, "rendered document is a plausible size");

    // Every substitutable placeholder must be gone. Checked by name
    // rather than by scanning for "{{", because the template's own
    // comments talk about the placeholder syntax.
    bool any_left = false;
    for (const char* key : {"locale", "timezone", "computer_name", "user_name", "user_password",
                            "product_key_block", "image_index", "win11_bypass_block"}) {
        if (xml.contains(QStringLiteral("{{%1}}").arg(QLatin1String(key)))) {
            std::printf("       unsubstituted: {{%s}}\n", key);
            any_left = true;
        }
    }
    check(!any_left, "no unsubstituted placeholders remain");

    // The inputs must actually reach the output.
    check(xml.contains(QStringLiteral("LSW-TEST")), "computer_name substituted");
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
    QStringList staged;
    check(PayloadIsoBuilder::stage_files(payload, staging.path(), &staged, nullptr),
          "stage_files() succeeds with only an answer file");
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

    // The display driver. accel-virt's package has to reach C:\av, and
    // SetupComplete has to sign it and install it — in that order, with
    // install's -NoVerifier, because the verifier is for testing — and
    // put the loaders where a process of either bitness finds them.
    check(!xml.contains(QStringLiteral("rvk")), "nothing of the render server's drivers is left");
    check(xml.contains(QStringLiteral("%i:\\av\\provision.ps1 xcopy /E /I /Y %i:\\av C:\\av")),
          "specialize copies \\av off the payload CD to C:\\av");
    // An untouched package for a GPU added after the install: the first
    // sign invalidates C:\av\package against its own package.sha256.
    check(xml.contains(QStringLiteral("xcopy /E /I /Y %i:\\av\\package C:\\av\\pristine\\package")),
          "specialize keeps a pristine copy of the package for later adapters");
    check(xml.contains(QStringLiteral("$avScript = 'C:\\av\\provision.ps1'")),
          "SetupComplete runs C:\\av\\provision.ps1");
    // Sign in specialize, install at SetupComplete: sign turns test
    // signing on, and only the reboot that ends specialize puts it in
    // force before pnputil runs.
    {
        const qsizetype copy = xml.indexOf(QStringLiteral("xcopy /E /I /Y %i:\\av C:\\av"));
        const qsizetype sign_cmd =
            xml.indexOf(QStringLiteral("-File \"C:\\Windows\\Setup\\Scripts\\LswAvSign.ps1\""));
        check(copy >= 0 && sign_cmd > copy,
              "specialize runs LswAvSign.ps1 after copying the package");
        check(sign_cmd >= 0 &&
                  sign_cmd < xml.indexOf(QStringLiteral("<settings pass=\"oobeSystem\">")),
              "and that command is in the specialize pass");
    }
    check(xml.contains(QStringLiteral("-File $avScript -Stage sign")) &&
              xml.contains(QStringLiteral("C:\\ProgramData\\LSW\\av-sign.log")),
          "LswAvSign.ps1 runs the sign stage and logs it");
    {
        const qsizetype resign = xml.indexOf(QStringLiteral("if (-not (AvSigned))"));
        const qsizetype install =
            xml.indexOf(QStringLiteral("AvStage @('-Stage', 'install', '-NoVerifier')"));
        check(resign >= 0 && install > resign,
              "SetupComplete signs again only if the catalog is not signed, then installs "
              "-NoVerifier");
    }
    check(xml.contains(QStringLiteral("$rc -eq 3010")),
          "a stage asking for a reboot is not a failure");
    check(xml.contains(QStringLiteral("@('x86', 'SysWOW64')")) &&
              xml.contains(QStringLiteral("@('x64', 'System32')")),
          "both loader sets are installed, the x86 one into SysWOW64");
    check(xml.contains(QStringLiteral("DontSearchWindowsUpdate")),
          "driver searches on Windows Update are off before OOBE has the network");

    // The agent is a LocalSystem service now, not a logon task as the
    // wizard user: registered with automatic start and restart-on-failure
    // at SetupComplete, ahead of the promotion's early exits, and started
    // there (SetupComplete is followed by the first logon, not a reboot).
    check(!xml.contains(QStringLiteral("Register-ScheduledTask -TaskName 'lsw-agent'")) &&
              !xml.contains(QStringLiteral("<LogonType>InteractiveToken</LogonType>")),
          "no lsw-agent logon task is registered any more");
    check(xml.contains(QStringLiteral("$svcBin = 'C:\\ProgramData\\LSW\\lsw-agent.exe --service'")),
          "the service runs the deployed exe in --service mode");
    check(xml.contains(QStringLiteral(
              "sc.exe create $svc binPath= $svcBin start= auto obj= LocalSystem")),
          "the service is created as LocalSystem with automatic start");
    check(xml.contains(QStringLiteral(
              "sc.exe config $svc binPath= $svcBin start= auto obj= LocalSystem")),
          "an existing service is reconfigured, so a second SetupComplete run is harmless");
    check(xml.contains(QStringLiteral(
              "sc.exe failure $svc reset= 86400 actions= restart/5000/restart/5000/restart/30000")) &&
              xml.contains(QStringLiteral("sc.exe failureflag $svc 1")),
          "the SCM restarts a failed agent, including one that exits non-zero");
    {
        const qsizetype promote = xml.indexOf(QStringLiteral("<File path=\"C:\\Windows\\Setup\\Scripts\\LswPromote.ps1\">"));
        const qsizetype create = xml.indexOf(QStringLiteral("sc.exe create $svc"));
        const qsizetype start = xml.indexOf(QStringLiteral("sc.exe start $svc"));
        const qsizetype early_exit = xml.indexOf(QStringLiteral("promoted.flag present - skipping"));
        check(promote >= 0 && create > promote && start > create && early_exit > start,
              "LswPromote.ps1 registers and starts the service before the promotion can exit");
    }
    check(xml.contains(QStringLiteral("localport=5102")),
          "the agent's port stays open in the firewall");
    check(xml.contains(QStringLiteral("HKU\\DefaultUser\\Control Panel\\Desktop\" /v ScreenSaveActive")),
          "the screen saver is off for new accounts, since setup steps no longer run as the user");

    // The package stages whole, as \av, and the Vulkan loaders land
    // beside its OpenCL ones.
    QTemporaryDir av_src;
    const QDir av_root{av_src.path()};
    const char* const av_files[] = {
        "provision.ps1",          "adapters.txt",           "package/avgpu.sys",
        "package/avgpu.inf",      "package/avumd.dll",      "package/avvk32.json",
        "package/package.sha256", "loaders/x64/OpenCL.dll", "loaders/x86/OpenCL.dll",
    };
    for (const char* name : av_files) {
        const QString path = av_root.filePath(QLatin1String(name));
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f{path};
        check(f.open(QIODevice::WriteOnly), "created a fake driver package file");
        f.write(QByteArray("stub for ") + name);
    }
    QTemporaryDir loader_src;
    const QString vulkan64 = QDir(loader_src.path()).filePath(QStringLiteral("vulkan-1.dll"));
    const QString vulkan32 = QDir(loader_src.path()).filePath(QStringLiteral("vulkan-1-x86.dll"));
    for (const QString& path : {vulkan64, vulkan32}) {
        QFile f{path};
        check(f.open(QIODevice::WriteOnly), "created a fake Vulkan loader");
        f.write("MZstub");
    }

    QTemporaryDir staging3;
    PayloadInputs with_av;
    with_av.autounattend_xml = xml;
    with_av.av_driver_tree = av_src.path();
    with_av.vulkan_loader_x64_path = vulkan64;
    with_av.vulkan_loader_x86_path = vulkan32;
    with_av.av_adapter_names = QStringLiteral("1002\t7550\tAMD Radeon RX 9070 XT\n");
    QString av_error;
    check(PayloadIsoBuilder::stage_files(with_av, staging3.path(), nullptr, &av_error),
          "stage_files() succeeds with the driver package");
    if (!av_error.isEmpty()) {
        std::printf("       %s\n", qPrintable(av_error));
    }
    for (const char* name : av_files) {
        check(QFile::exists(
                  QDir(staging3.path()).filePath(QStringLiteral("av/") + QLatin1String(name))),
              "driver package file staged under \\av, at its place in the tree");
    }
    for (const char* name : {"av/loaders/x64/vulkan-1.dll", "av/loaders/x86/vulkan-1.dll"}) {
        check(QFile::exists(QDir(staging3.path()).filePath(QLatin1String(name))),
              "Vulkan loader staged beside the package's OpenCL loader");
    }
    // The package's adapters.txt names the GPUs of the machine that
    // built it; the guest gets this machine's.
    {
        QFile names{QDir(staging3.path()).filePath(QStringLiteral("av/adapters.txt"))};
        check(names.open(QIODevice::ReadOnly) &&
                  names.readAll() == with_av.av_adapter_names.toUtf8(),
              "\\av\\adapters.txt is this host's GPU names, not the package's");
    }
    {
        QTemporaryDir staging_unnamed;
        PayloadInputs unnamed = with_av;
        unnamed.av_adapter_names.clear();
        check(PayloadIsoBuilder::stage_files(unnamed, staging_unnamed.path(), nullptr, nullptr) &&
                  !QFile::exists(
                      QDir(staging_unnamed.path()).filePath(QStringLiteral("av/adapters.txt"))),
              "with no host GPU to name, no adapters.txt, not even the package's");
    }

    // A directory that is not the package is refused, not half-staged:
    // pnputil refuses a package missing a file its INF names, and that
    // would surface as a guest with no display after a full install.
    QTemporaryDir not_av;
    PayloadInputs with_bad_av;
    with_bad_av.autounattend_xml = xml;
    with_bad_av.av_driver_tree = not_av.path();
    QTemporaryDir staging4;
    QString bad_error;
    check(!PayloadIsoBuilder::stage_files(with_bad_av, staging4.path(), nullptr, &bad_error),
          "a directory with no provision.ps1 is refused as a driver package");
    check(bad_error.contains(QStringLiteral("LSW_AV_DRIVERS")),
          "and the error says how to point at a real one");

    // End to end: build a real ISO and read the answer file back out of
    // it. This is the step that actually failed in the field — the file
    // was present at the ISO root but zero bytes, so Windows Setup
    // silently fell through to the interactive installer.
    QTemporaryDir iso_dir;
    PayloadInputs iso_inputs;
    iso_inputs.autounattend_xml = xml;
    iso_inputs.output_iso_path = QDir(iso_dir.path()).filePath("payload.iso");
    // The driver package too: it is the one nested tree on the ISO.
    iso_inputs.av_driver_tree = av_src.path();
    iso_inputs.vulkan_loader_x64_path = vulkan64;
    // Optional: stage a real agent binary too, so the test exercises the
    // same multi-file staging the wizard does rather than a lone XML.
    if (argc > 1) {
        iso_inputs.agent_exe_path = QString::fromLocal8Bit(argv[1]);
        std::printf("       also staging: %s\n", argv[1]);
    }
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
        check(raw.contains("stub for package/avgpu.inf"),
              "the driver package's nested files are in the ISO");
    }

    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
