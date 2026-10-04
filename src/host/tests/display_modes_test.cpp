// The guest's resolution list: what accel-virt's driver will take, and
// the forms the list is stored and sent in. The limits are the driver's
// (external/accel-virt/src/miniport/display/display_mode.*): a mode the
// editor accepts and the driver drops is a resolution that silently
// never appears in Windows.

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/services/domain_inputs.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

using lsw::host::config::DisplayMode;
using lsw::host::config::DisplayModes;
using lsw::host::config::ManagerSettings;
using K = ManagerSettings::Key;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

bool fits(int w, int h) {
    return DisplayModes::refusal(DisplayMode{w, h}).isEmpty();
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    // ---- parsing and naming ----------------------------------------
    check(DisplayModes::parse(QStringLiteral("2560x1440")) == DisplayMode{2560, 1440},
          "WxH parses");
    check(DisplayModes::parse(QStringLiteral(" 1920 X 1080 ")) == DisplayMode{1920, 1080},
          "upper-case X and spaces parse");
    check(!DisplayModes::parse(QStringLiteral("1920")).has_value() &&
              !DisplayModes::parse(QStringLiteral("x1080")).has_value() &&
              !DisplayModes::parse(QStringLiteral("1920x1080x60")).has_value(),
          "anything else does not");
    check(DisplayMode{2560, 1440}.text() == QStringLiteral("2560x1440"), "text is WxH");
    check(DisplayMode{2560, 1440}.aspect() == QStringLiteral("16:9") &&
              DisplayMode{3440, 1440}.aspect() == QStringLiteral("43:18") &&
              DisplayMode{1920, 1200}.aspect() == QStringLiteral("8:5"),
          "aspect is the reduced ratio");

    // ---- the driver's limits ---------------------------------------
    check(fits(100, 100) && !fits(99, 100) && !fits(100, 99), "100 is the smallest edge");
    check(fits(4095, 1000) && !fits(4096, 1000) && !fits(1000, 4096), "4095 is the largest edge");
    check(DisplayModes::pixel_clock_hz(DisplayMode{3840, 2160}) == 540'000'000,
          "3840x2160 is a 540 MHz clock, as the driver times it");
    check(fits(3840, 2160), "3840x2160 fits");
    check(!fits(4095, 4095), "4095x4095 is past the clock");
    // (4000 + 160) * (2300 + 95) * 60 = 597.8 MHz; (4095 + 160) * (2600 +
    // 108) * 60 = 691.1 MHz.
    check(fits(4000, 2300) && !fits(4095, 2600), "the clock limit is 655.35 MHz");
    check(DisplayModes::refusal(DisplayMode{5120, 2880}).contains(QStringLiteral("4095")),
          "a refusal says why");

    // ---- the list ---------------------------------------------------
    const QStringList list = {QStringLiteral("2560x1440"), QStringLiteral("1920X1080"),
                              QStringLiteral("2560x1440"), QStringLiteral("junk"),
                              QStringLiteral("5000x5000")};
    check(DisplayModes::normalized(list) ==
              QStringList({QStringLiteral("2560x1440"), QStringLiteral("1920x1080")}),
          "normalized drops junk, refused sizes and duplicates, keeps order");
    check(!DisplayModes::refusal_to_add(list, DisplayMode{1920, 1080}).isEmpty(),
          "a duplicate cannot be added");
    check(DisplayModes::refusal_to_add(list, DisplayMode{1280, 720}).isEmpty(),
          "a new size can");
    QStringList full;
    for (int i = 0; i < DisplayModes::kMaxModes; ++i) {
        full << DisplayMode{800 + i, 600}.text();
    }
    check(!DisplayModes::refusal_to_add(full, DisplayMode{1280, 720}).isEmpty(),
          "a full list takes no more");
    check(DisplayModes::effective(list, DisplayMode{1920, 1080}) ==
              QStringList({QStringLiteral("1920x1080"), QStringLiteral("2560x1440")}),
          "the monitor's size goes first, once");
    check(DisplayModes::effective({}, std::nullopt).isEmpty(),
          "no list and no monitor is the host monitors' modes");
    check(DisplayModes::property({QStringLiteral("2560x1440"), QStringLiteral("1920x1080")}) ==
              QStringLiteral("2560x1440,1920x1080"),
          "the property is the list between commas");

    // ---- settings ---------------------------------------------------
    QTemporaryDir dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    {
        ManagerSettings settings;
        settings.set_value(K::kDisplayModes, QStringList{QStringLiteral("2560x1440")});
        check(settings.string_list_value(K::kDisplayModes) ==
                  QStringList{QStringLiteral("2560x1440")},
              "a stored list reads back");
        check(lsw::host::services::display_modes_from_settings(settings) ==
                  QStringLiteral("2560x1440"),
              "the device is given the stored size");
        settings.set_value(K::kMatchHostOutput, true);
        check(lsw::host::services::display_modes_from_settings(settings, DisplayMode{3840, 2160}) ==
                  QStringLiteral("3840x2160,2560x1440"),
              "matching the monitor puts its size first");
        check(lsw::host::services::display_modes_from_settings(settings) ==
                  QStringLiteral("2560x1440"),
              "with no monitor known the list goes as it is");
        settings.set_value(K::kDisplayModes, QStringList{});
    }
    {
        ManagerSettings settings;
        check(settings.string_list_value(K::kDisplayModes).isEmpty(),
              "an empty list stays empty");
    }

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
