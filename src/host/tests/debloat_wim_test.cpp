// Runs the debloater against a real Windows image.
//
// Opt-in: set ``LSW_TEST_WIM`` to a WRITABLE COPY of an install.wim
// extracted from Windows media, and ``LSW_TEST_WIM_INDEX`` to the
// edition index to operate on. Without it the test reports skipped and
// passes, because CI has no Windows media and downloading eight
// gigabytes to find out whether a prefix matches is not a unit test.
//
// The copy is modified in place. Point it at a scratch file.
//
// What this actually checks is the half that cannot be checked any
// other way: that the appx payload directory is where the code looks,
// that the prefixes match the names Microsoft ships, and — the one that
// matters most — that nothing the rest of the image depends on is
// caught by them.

#include "lsw/host/provision/debloat/debloat_level.hpp"
#include "lsw/host/provision/debloat/wim_debloater.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("debloat_wim: the debloater against real Windows media\n");

    const QString wim = qEnvironmentVariable("LSW_TEST_WIM");
    if (wim.isEmpty() || !QFileInfo::exists(wim)) {
        std::printf("  SKIP  set LSW_TEST_WIM to a writable install.wim copy\n");
        std::printf("RESULT PASS 0/0 (skipped)\n");
        return 0;
    }
    const int index = qEnvironmentVariable("LSW_TEST_WIM_INDEX", QStringLiteral("1")).toInt();

    using namespace lsw::host::provision::debloat;
    // Sane by default because it is the level the wizard recommends and
    // the one whose assertions below are about. LSW_TEST_WIM_LEVEL=maximum
    // measures the component-store and driver passes instead, which is
    // the run worth doing when either of those lists changes.
    const DebloatLevel level =
        DebloatPlan::from_string(qEnvironmentVariable("LSW_TEST_WIM_LEVEL",
                                                      QStringLiteral("sane")));
    const qint64 before = QFileInfo(wim).size();
    std::printf("  image: %s (%lld MiB), index %d, level %s\n", qPrintable(wim),
                static_cast<long long>(before / (1024 * 1024)), index,
                qPrintable(DebloatPlan::to_string(level)));

    // Isolate first, exactly as DebloatStager does: without it the
    // deletions free almost nothing, because the other editions in the
    // file still reference the same resources.
    const QString single = wim + QStringLiteral(".single");
    QString isolate_error;
    const bool isolated = WimDebloater::isolate_edition(wim, index, single, &isolate_error);
    check(isolated, "isolate_edition() succeeded");
    if (!isolated) {
        std::printf("  error: %s\n", qPrintable(isolate_error));
        std::printf("RESULT FAIL %d\n", failures);
        return 1;
    }
    const qint64 isolated_size = QFileInfo(single).size();
    std::printf("  isolated one edition: %lld MiB -> %lld MiB\n",
                static_cast<long long>(before / (1024 * 1024)),
                static_cast<long long>(isolated_size / (1024 * 1024)));
    check(isolated_size < before, "isolating one edition shrank the image");

    WimDebloater debloater;
    QObject::connect(&debloater, &WimDebloater::progress,
                     [](const QString& phase, int percent) {
                         std::printf("  [%3d%%] %s\n", percent, qPrintable(phase));
                         std::fflush(stdout);
                     });

    QString error;
    // Index 1: the isolated file has exactly one image.
    const bool ok = debloater.apply(single, 1, DebloatPlan{level}, &error);
    check(ok, "apply() succeeded");
    if (!ok) {
        std::printf("  error: %s\n", qPrintable(error));
        std::printf("RESULT FAIL %d\n", failures);
        return 1;
    }

    const QStringList removed = debloater.removed();
    std::printf("  removed %lld paths\n", static_cast<long long>(removed.size()));
    check(!removed.isEmpty(), "something was matched and removed");

    // The appx payloads have to have been found. A run that removed only
    // the OneDrive stub means the directory moved and the prefixes never
    // matched anything — which would pass silently without this.
    const auto appx =
        removed.filter(QStringLiteral("/Program Files/WindowsApps/"));
    check(appx.size() > 5, "provisioned application payloads were matched");

    // And the packages everything else links against must NOT be in it.
    // This is the assertion worth having: a prefix that is too broad
    // produces an image that installs and then cannot run an app.
    for (const char* keep : {"Microsoft.VCLibs", "Microsoft.UI.Xaml", "Microsoft.NET.Native",
                             "Microsoft.WindowsStore", "Microsoft.DesktopAppInstaller",
                             "Microsoft.WindowsAppRuntime", "Microsoft.WindowsNotepad",
                             "Microsoft.WindowsCalculator", "Microsoft.Windows.Photos",
                             "Microsoft.SecHealthUI", "Microsoft.ScreenSketch"}) {
        const bool caught = !removed.filter(QLatin1String(keep)).isEmpty();
        check(!caught, (QStringLiteral("kept: ") + QLatin1String(keep)).toUtf8().constData());
    }

    const qint64 after = QFileInfo(single).size();
    std::printf("  debloated: %lld MiB -> %lld MiB (from %lld MiB of media)\n",
                static_cast<long long>(isolated_size / (1024 * 1024)),
                static_cast<long long>(after / (1024 * 1024)),
                static_cast<long long>(before / (1024 * 1024)));
    check(after < isolated_size, "the debloat shrank it further");

    std::printf("RESULT %s %d failures\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
