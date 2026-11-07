#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/vram_reserve.hpp"
#include "lsw/host/services/domain_inputs.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

namespace {

using lsw::host::config::ManagerSettings;
using lsw::host::config::VramReserve;
using K = ManagerSettings::Key;

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    const auto big = VramReserve::resolve(0, 16384);
    check(big.reserve_mib == 2048 && big.guest_cap_mib == 14336, "Auto keeps an eighth of 16 GiB");
    const auto mid = VramReserve::resolve(0, 8192);
    check(mid.reserve_mib == 1024 && mid.guest_cap_mib == 7168, "Auto keeps 1 GiB of 8 GiB");
    const auto small = VramReserve::resolve(0, 1536);
    check(small.guest_cap_mib == 768 && small.reserve_mib == 1024,
          "Auto never leaves the guest under half");
    check(VramReserve::resolve(0, 2048).guest_cap_mib == 1024, "Auto on 2 GiB gives half");

    const auto four = VramReserve::resolve(4096, 16384);
    check(four.reserve_mib == 4096 && four.guest_cap_mib == 12288, "an explicit reserve is taken");
    check(VramReserve::resolve(64, 16384).reserve_mib == 256, "an explicit reserve is at least 256 MiB");
    const auto greedy = VramReserve::resolve(16384, 16384);
    check(greedy.reserve_mib == 15872 && greedy.guest_cap_mib == 8192,
          "the reserve leaves 512 MiB and the guest cap never drops under half");
    check(VramReserve::resolve(8192, 8192).reserve_mib == 7680, "a reserve of all of it leaves 512 MiB");
    check(VramReserve::resolve(12288, 16384).guest_cap_mib == 8192, "the half rule holds the cap");
    check(VramReserve::resolve(0, 0).guest_cap_mib == 0 && VramReserve::resolve(2048, 0).guest_cap_mib == 0,
          "an unknown total leaves nothing to compute");
    check(VramReserve::label(0) == QStringLiteral("Auto") &&
              VramReserve::label(2048) == QStringLiteral("2 GiB") &&
              VramReserve::label(1536) == QStringLiteral("1536 MiB"),
          "labels");

    QTemporaryDir dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    {
        ManagerSettings settings;
        check(ManagerSettings::needs_restart(K::kVramReserveMib), "the reserve applies at the next start");
        check(settings.int_value(K::kVramReserveMib) == 0, "the default is Auto");
        lsw::host::config::Settings paths;
        check(lsw::host::services::domain_inputs_from_settings(paths, settings, std::nullopt)
                      .vram_reserve_mib == 0,
              "Auto reaches the domain inputs as 0");
        settings.set_value(K::kVramReserveMib, 3072);
        check(lsw::host::services::domain_inputs_from_settings(paths, settings, std::nullopt)
                      .vram_reserve_mib == 3072,
              "a stored reserve reaches the domain inputs");
    }

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
