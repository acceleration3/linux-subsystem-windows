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

}

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
    const DebloatLevel level =
        DebloatPlan::from_string(qEnvironmentVariable("LSW_TEST_WIM_LEVEL",
                                                      QStringLiteral("sane")));
    const qint64 before = QFileInfo(wim).size();
    std::printf("  image: %s (%lld MiB), index %d, level %s\n", qPrintable(wim),
                static_cast<long long>(before / (1024 * 1024)), index,
                qPrintable(DebloatPlan::to_string(level)));

    const QString single = wim + QStringLiteral(".single");
    QString isolate_error;
    const bool isolated =
        WimDebloater::isolate_edition(wim, {}, index, single, &isolate_error);
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

    const auto appx =
        removed.filter(QStringLiteral("/Program Files/WindowsApps/"));
    check(appx.size() > 5, "provisioned application payloads were matched");

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
