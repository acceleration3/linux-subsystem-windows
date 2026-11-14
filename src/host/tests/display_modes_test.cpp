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

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

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

    check(fits(100, 100) && !fits(99, 100) && !fits(100, 99), "100 is the smallest edge");
    check(fits(8192, 1000) && !fits(8193, 1000) && !fits(1000, 8193), "8192 is the largest edge");
    check(fits(3840, 2160) && fits(8192, 8192), "no size is refused for its pixel clock");
    check(fits(5120, 1440) && fits(7680, 4320), "an ultrawide and an 8K monitor's sizes are accepted");
    check(DisplayModes::refusal(DisplayMode{8193, 2880}).contains(QStringLiteral("8192"))
              && DisplayModes::refusal(DisplayMode{2880, 8193}).contains(QStringLiteral("8192")),
          "a refusal names the limit");
    check(DisplayModes::refusal(DisplayMode{99, 2880}).contains(QStringLiteral("100")),
          "also the lower one");

    const QStringList wide = {QStringLiteral("5120x1440"), QStringLiteral("7680x4320"),
                              QStringLiteral("8193x1440"), QStringLiteral("1920x1080")};
    check(DisplayModes::normalized(wide)
              == QStringList({QStringLiteral("5120x1440"), QStringLiteral("7680x4320"),
                              QStringLiteral("1920x1080")}),
          "sizes wider than 3840 stay in the configured list, one past the limit does not");
    check(DisplayModes::refusal_to_add({QStringLiteral("1920x1080")}, DisplayMode{5120, 1440}).isEmpty()
              && DisplayModes::refusal_to_add({QStringLiteral("1920x1080")}, DisplayMode{7680, 4320}).isEmpty(),
          "and can be added to it");
    check(DisplayModes::effective({QStringLiteral("1920x1080")}, DisplayMode{5120, 1440})
              == QStringList({QStringLiteral("5120x1440"), QStringLiteral("1920x1080")}),
          "an ultrawide monitor's size goes first");
    check(DisplayModes::effective({QStringLiteral("1920x1080")}, DisplayMode{10240, 4320})
              == QStringList({QStringLiteral("1920x1080")}),
          "a monitor past the limit is not offered");

    const QVector<DisplayMode>& common = DisplayModes::common();
    bool common_fit = true;
    for (const DisplayMode& mode : common) {
        common_fit = common_fit && fits(mode.width, mode.height);
    }
    check(common_fit, "every common size is one the display accepts");
    check(common.contains(DisplayMode{3440, 1440}) && common.contains(DisplayMode{5120, 1440})
              && common.contains(DisplayMode{5120, 2160}) && common.contains(DisplayMode{7680, 2160})
              && common.contains(DisplayMode{7680, 4320}),
          "the common sizes reach the ultrawide and 8K ones");
    const QVector<DisplayMode> offered = DisplayModes::offerable(
        {DisplayMode{5120, 1440}, DisplayMode{6880, 2880}, DisplayMode{10240, 4320},
         DisplayMode{6880, 2880}});
    check(offered.value(0) == (DisplayMode{5120, 1440}) && offered.value(1) == (DisplayMode{6880, 2880})
              && offered.size() == common.size() + 1,
          "the sizes to pick from start with the monitors' own, each once");
    check(!offered.contains(DisplayMode{10240, 4320}),
          "a monitor past the limit is not among them");

    const QStringList list = {QStringLiteral("2560x1440"), QStringLiteral("1920X1080"),
                              QStringLiteral("2560x1440"), QStringLiteral("junk"),
                              QStringLiteral("9000x9000")};
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
        check(lsw::host::services::display_modes_from_settings(settings, DisplayMode{5120, 1440}) ==
                  QStringLiteral("5120x1440,2560x1440"),
              "an ultrawide monitor's size is sent to the device");
        settings.set_value(K::kMatchHostOutput, false);
        settings.set_value(K::kDisplayModes, QStringList{QStringLiteral("5120x1440"),
                                                         QStringLiteral("7680x4320"),
                                                         QStringLiteral("8193x4320")});
        check(lsw::host::services::display_modes_from_settings(settings) ==
                  QStringLiteral("5120x1440,7680x4320"),
              "configured sizes up to the limit are sent to the device, one past it is not");
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
