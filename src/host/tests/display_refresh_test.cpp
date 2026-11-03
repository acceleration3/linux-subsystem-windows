#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/platform/host_monitors.hpp"
#include "lsw/host/services/domain_inputs.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

namespace {

using lsw::host::config::DisplayRefresh;
using lsw::host::config::ManagerSettings;
using lsw::host::platform::HostMonitors;
using lsw::host::platform::HostMonitor;
using lsw::host::platform::RateChoice;
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

    check(!DisplayRefresh::fixed(QStringLiteral("monitor")).has_value(), "monitor is no rate");
    check(DisplayRefresh::fixed(QStringLiteral("359980")) == 359980u, "a rate is its millihertz");
    check(DisplayRefresh::fixed(QStringLiteral(" 60000 ")) == 60000u, "spaces are allowed");
    check(!DisplayRefresh::fixed(QStringLiteral("999")).has_value() &&
              !DisplayRefresh::fixed(QStringLiteral("1000001")).has_value() &&
              !DisplayRefresh::fixed(QStringLiteral("-5")).has_value() &&
              !DisplayRefresh::fixed(QStringLiteral("60.5")).has_value() &&
              !DisplayRefresh::fixed(QString{}).has_value(),
          "nothing outside 1 Hz to 1 kHz, and no non-number, is a rate");
    check(DisplayRefresh::fixed(QStringLiteral("1000")) == 1000u &&
              DisplayRefresh::fixed(QStringLiteral("1000000")) == 1'000'000u,
          "the ends are rates");
    check(DisplayRefresh::from_hertz(359.979) == 359979u, "a screen's hertz rounds to the millihertz");
    check(DisplayRefresh::from_hertz(59.9399) == 59940u, "and rounds, not truncates");
    check(!DisplayRefresh::from_hertz(0.0).has_value() && !DisplayRefresh::from_hertz(-1.0).has_value() &&
              !DisplayRefresh::from_hertz(1001.0).has_value() &&
              !DisplayRefresh::from_hertz(std::nan("")).has_value(),
          "no screen rate is not a rate");
    check(DisplayRefresh::resolve(QStringLiteral("144000"), 360000u) == 144000u,
          "a chosen rate wins over the monitor's");
    check(DisplayRefresh::resolve(QStringLiteral("monitor"), 360000u) == 360000u,
          "monitor is the monitor's rate");
    check(!DisplayRefresh::resolve(QStringLiteral("monitor"), std::nullopt).has_value(),
          "and nothing while that is not known");
    check(DisplayRefresh::resolve(QStringLiteral("junk"), 75000u) == 75000u, "a junk setting is monitor");
    check(DisplayRefresh::label(360000u) == QStringLiteral("360 Hz") &&
              DisplayRefresh::label(239760u) == QStringLiteral("239.76 Hz") &&
              DisplayRefresh::label(143981u) == QStringLiteral("143.981 Hz") &&
              DisplayRefresh::label(60050u) == QStringLiteral("60.05 Hz") &&
              DisplayRefresh::label(1000000u) == QStringLiteral("1000 Hz"),
          "a rate reads without decimals when whole, else to three with trailing zeros dropped");
    check(DisplayRefresh::label(std::nullopt) == QStringLiteral("-"), "an unknown rate is a hyphen");

    check(HostMonitors::mode_millihz(148500, 2200, 1125) == 60000, "1080p60 is 60 Hz");
    check(HostMonitors::mode_millihz(148352, 2200, 1125) == 59940, "1080p59.94 is 59.94 Hz");
    check(HostMonitors::mode_millihz(1'000'000, 2000, 1389) == 359971,
          "a 360 Hz mode is its exact rate, not its name");
    check(HostMonitors::mode_millihz(0, 2200, 1125) == 0 &&
              HostMonitors::mode_millihz(148500, 0, 1125) == 0 &&
              HostMonitors::mode_millihz(148500, 2200, 0) == 0,
          "a mode with no frame or no clock has no rate");
    check(HostMonitors::mode_millihz(0xFFFFFFFFu, 1, 1) > 0, "a huge clock overflows nothing");

    const QVector<HostMonitor> monitors = {
        {QStringLiteral("DP-2"), {360000, 239760, 144000, 60000, 59940}},
        {QStringLiteral("HDMI-A-1"), {144000, 119880, 60000}},
    };
    const QVector<HostMonitor> screens = {{QStringLiteral("DP-2"), {359979}}};
    const QVector<RateChoice> merged = HostMonitors::merge(monitors, screens);
    check(merged.size() == 5, "five choices: 360, 239.76, 144, 119.88, 60");
    check(merged.size() == 5 && merged[0].millihz == 360000 && merged[1].millihz == 239760 &&
              merged[2].millihz == 144000 && merged[3].millihz == 119880 && merged[4].millihz == 60000,
          "highest first, within half a hertz one choice shown as the highest");
    check(merged.size() == 5 && merged[0].monitors == QStringList{QStringLiteral("DP-2")},
          "the screen's 359.979 is the monitor's 360, named once");
    check(merged.size() == 5 &&
              merged[2].monitors == (QStringList{QStringLiteral("DP-2"), QStringLiteral("HDMI-A-1")}) &&
              merged[4].monitors == (QStringList{QStringLiteral("DP-2"), QStringLiteral("HDMI-A-1")}),
          "a rate both support names both");
    check(HostMonitors::merge({}, {}).isEmpty(), "nothing read and no screen is no choice");
    {
        const auto real = HostMonitors::merge(
            {{QStringLiteral("DP-2"), {75029, 75025, 75000, 74973, 74551, 72809, 60317, 60020, 60004, 60000, 59954, 59940}}},
            {});
        check(real.size() == 3 && real[0].millihz == 75000 && real[1].millihz == 72809 && real[2].millihz == 60000,
              "a choice is shown as its rate nearest a whole hertz");
    }
    {
        const auto only_screens = HostMonitors::merge({}, screens);
        check(only_screens.size() == 1 && only_screens[0].millihz == 359979,
              "with no node readable the screens' own rates are the list");
    }
    {
        const auto edge = HostMonitors::merge({{QStringLiteral("A"), {60000, 59501, 59500}}}, {});
        check(edge.size() == 2 && edge[0].millihz == 60000 && edge[1].millihz == 59500,
              "499 mHz apart is one choice, 500 are two");
        const auto chain = HostMonitors::merge({{QStringLiteral("A"), {60000, 59700, 59400}}}, {});
        check(chain.size() == 2 && chain[1].millihz == 59400, "a choice reaches half a hertz from its highest only");
    }

    QTemporaryDir dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    {
        ManagerSettings settings;
        check(!ManagerSettings::needs_restart(K::kDisplayRefresh), "the refresh is live");
        check(lsw::host::services::display_refresh_from_settings(settings, 144000u) == 144000u,
              "the default follows the monitor");
        check(!lsw::host::services::display_refresh_from_settings(settings).has_value(),
              "and says nothing while the monitor is not known");
        settings.set_value(K::kDisplayRefresh, QStringLiteral("239760"));
        check(lsw::host::services::display_refresh_from_settings(settings, 144000u) == 239760u,
              "a stored rate is the rate");
        settings.set_value(K::kDisplayRefresh, QStringLiteral("monitor"));
    }

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
