#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/services/one_to_one_desktop.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QSize>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>

using lsw::host::config::DisplayMode;
using lsw::host::config::DisplayModes;
using lsw::host::config::ManagerSettings;
using lsw::host::services::OneToOneDesktop;
using K = ManagerSettings::Key;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void spin(int ms) {
    for (const int wait : {ms, 10, 10, 10}) {
        QEventLoop loop;
        QTimer::singleShot(wait, &loop, &QEventLoop::quit);
        loop.exec();
    }
}

constexpr int kSettle = 30;
constexpr int kQuiet = 150;

struct FakeGuest {
    QStringList offers;
    QStringList switches;
    QStringList device;
    DisplayMode now{2560, 1440};
    bool running = true;
    bool agent = true;
    int reads = 0;

    std::optional<DisplayMode> answer(const std::optional<DisplayMode>& wanted) {
        if (!agent) {
            return std::nullopt;
        }
        if (!wanted) {
            ++reads;
            return now;
        }
        switches << wanted->text();
        if (device.contains(wanted->text())) {
            now = *wanted;
        }
        return now;
    }

    OneToOneDesktop::Guest hooks() {
        OneToOneDesktop::Guest guest;
        guest.offer = [this](const QString& modes) {
            if (!running) {
                return false;
            }
            offers << modes;
            device = modes.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (!device.contains(now.text()) && !device.isEmpty()) {
                now = *DisplayModes::parse(device.first());
            }
            return true;
        };
        guest.resolution = [this](const std::optional<DisplayMode>& wanted,
                                  OneToOneDesktop::Answer done) {
            QTimer::singleShot(1, qApp, [this, wanted, done] { done(answer(wanted)); });
        };
        guest.resolution_now = [this](const std::optional<DisplayMode>& wanted) {
            return answer(wanted);
        };
        return guest;
    }
};

const QStringList kConfigured{QStringLiteral("2560x1440"), QStringLiteral("1920x1080")};
const QString kConfiguredProperty = QStringLiteral("2560x1440,1920x1080");

void reset(ManagerSettings& settings, bool enabled) {
    settings.set_value(K::kDisplayModes, kConfigured);
    settings.set_value(K::kMatchHostOutput, false);
    settings.set_value(K::kOneToOneDesktop, enabled);
    settings.set_value(K::kDesktopRestore, QString());
}

void off_changes_nothing(ManagerSettings& settings) {
    reset(settings, false);
    FakeGuest guest;
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
    desktop.opened();
    desktop.resized(QSize(1200, 840));
    spin(kQuiet);
    desktop.closed();
    spin(kQuiet);
    check(guest.offers.isEmpty() && guest.switches.isEmpty() && guest.reads == 0,
          "with the setting off the guest is told and asked nothing");
    check(desktop.modes(std::nullopt) == kConfiguredProperty,
          "and the device's modes are the configured ones");
    check(desktop.modes(DisplayMode{3840, 2160}) == kConfiguredProperty,
          "which a monitor's size does not lead unless matching it is on");
    settings.set_value(K::kMatchHostOutput, true);
    check(desktop.modes(DisplayMode{3840, 2160}) == QStringLiteral("3840x2160,2560x1440,1920x1080"),
          "and does lead when it is");
}

void follows_the_window_and_restores(ManagerSettings& settings) {
    reset(settings, true);
    FakeGuest guest;
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
    desktop.opened();
    spin(kQuiet);
    check(guest.offers.isEmpty() && guest.switches.isEmpty(),
          "opening the desktop alone changes nothing: no size is known yet");

    desktop.resized(QSize(1200, 840));
    spin(kQuiet);
    check(guest.offers == QStringList{QStringLiteral("1200x840")},
          "a settled size is offered to Windows as the display's only mode");
    check(guest.switches == QStringList{QStringLiteral("1200x840")} &&
              guest.now == (DisplayMode{1200, 840}),
          "and Windows is switched to it");
    check(desktop.applied() == std::optional<DisplayMode>(DisplayMode{1200, 840}) &&
              desktop.modes(std::nullopt) == QStringLiteral("1200x840"),
          "the device's modes are that size for as long as it is applied");
    check(settings.string_value(K::kDesktopRestore) == QStringLiteral("2560x1440"),
          "the resolution Windows had is kept where a later start finds it");
    check(settings.string_list_value(K::kDisplayModes) == kConfigured,
          "the configured list is not touched");

    for (int width = 1201; width <= 1300; width += 11) {
        desktop.resized(QSize(width, 900));
    }
    desktop.resized(QSize(1300, 900));
    spin(kQuiet);
    check(guest.offers.size() == 2 && guest.offers.last() == QStringLiteral("1300x900") &&
              guest.switches.size() == 2 && guest.switches.last() == QStringLiteral("1300x900"),
          "a burst of sizes is one change, to the last of them");

    desktop.resized(QSize(1300, 900));
    spin(kQuiet);
    check(guest.offers.size() == 2 && guest.switches.size() == 2,
          "the same size again changes nothing");

    desktop.resized(QSize(5120, 1440));
    spin(kQuiet);
    check(guest.offers.last() == QStringLiteral("5120x1440") &&
              guest.now == (DisplayMode{5120, 1440}),
          "an ultrawide window's size is offered to the pixel");
    desktop.resized(QSize(9000, 60));
    spin(kQuiet);
    check(guest.offers.last() == QStringLiteral("8192x100") &&
              guest.now == (DisplayMode{8192, 100}),
          "a size past what the display can offer is the nearest it can");
    desktop.resized(QSize(9200, 40));
    spin(kQuiet);
    check(guest.offers.size() == 4, "and another size past it is that same mode, not a change");
    check(settings.string_value(K::kDesktopRestore) == QStringLiteral("2560x1440"),
          "what to return to stays the resolution from before the desktop opened");

    desktop.closed();
    spin(kQuiet);
    check(guest.offers.last() == kConfiguredProperty,
          "closing the desktop offers the configured modes again");
    check(guest.switches.last() == QStringLiteral("2560x1440") &&
              guest.now == (DisplayMode{2560, 1440}),
          "and returns Windows to the resolution it had");
    check(!desktop.applied().has_value() && settings.string_value(K::kDesktopRestore).isEmpty(),
          "after which nothing is left to restore");
    check(settings.string_list_value(K::kDisplayModes) == kConfigured,
          "the configured list is still as it was");

    const int offers = static_cast<int>(guest.offers.size());
    desktop.resized(QSize(1000, 700));
    spin(kQuiet);
    check(guest.offers.size() == offers, "a size arriving after the desktop closed is ignored");
}

void keeps_a_resolution_that_is_not_preferred(ManagerSettings& settings) {
    reset(settings, true);
    FakeGuest guest;
    guest.now = DisplayMode{1920, 1080};
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
    desktop.opened();
    desktop.resized(QSize(1600, 1000));
    spin(kQuiet);
    desktop.closed();
    spin(kQuiet);
    check(guest.offers.last() == kConfiguredProperty && guest.now == (DisplayMode{1920, 1080}),
          "Windows returns to the resolution it had, not to the preferred one");
}

void a_window_already_that_size_changes_nothing(ManagerSettings& settings) {
    reset(settings, true);
    FakeGuest guest;
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
    desktop.opened();
    desktop.resized(QSize(2560, 1440));
    spin(kQuiet);
    check(guest.offers.isEmpty() && guest.switches.isEmpty() &&
              settings.string_value(K::kDesktopRestore).isEmpty(),
          "a window that already holds Windows' resolution changes nothing");
    desktop.closed();
    spin(kQuiet);
    check(guest.offers.isEmpty() && guest.switches.isEmpty(),
          "and closing it has nothing to put back");
}

void shutdown_restores(ManagerSettings& settings) {
    reset(settings, true);
    FakeGuest guest;
    {
        OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
        desktop.opened();
        desktop.resized(QSize(1200, 840));
        spin(kQuiet);
        check(guest.now == (DisplayMode{1200, 840}),
              "the desktop is at the window's size before the manager exits");
        desktop.shutdown();
        check(guest.offers.last() == kConfiguredProperty &&
                  guest.now == (DisplayMode{2560, 1440}) &&
                  settings.string_value(K::kDesktopRestore).isEmpty(),
              "the manager exiting restores the modes and the resolution before it is gone");
    }
    {
        FakeGuest idle;
        OneToOneDesktop desktop(settings, idle.hooks(), nullptr, kSettle);
        desktop.shutdown();
        check(idle.offers.isEmpty() && idle.switches.isEmpty(),
              "an exit with nothing applied touches nothing");
    }
}

void a_stale_state_is_restored(ManagerSettings& settings) {
    reset(settings, true);
    settings.set_value(K::kDesktopRestore, QStringLiteral("1920x1080"));
    FakeGuest guest;
    guest.device = QStringList{QStringLiteral("1200x840")};
    guest.now = DisplayMode{1200, 840};
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
    check(desktop.modes(std::nullopt) == kConfiguredProperty,
          "a manager that starts offers the configured modes whatever an earlier one left");
    desktop.guest_ready();
    spin(kQuiet);
    check(guest.offers == QStringList{kConfiguredProperty} &&
              guest.now == (DisplayMode{1920, 1080}) &&
              settings.string_value(K::kDesktopRestore).isEmpty(),
          "a guest left at a desktop window's size by a manager that died is put back when it is "
          "reachable");
    desktop.guest_ready();
    spin(kQuiet);
    check(guest.offers.size() == 1 && guest.switches.size() == 1, "and only once");
}

void an_unreachable_guest_is_restored_later(ManagerSettings& settings) {
    reset(settings, true);
    FakeGuest guest;
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
    guest.agent = false;
    desktop.opened();
    desktop.resized(QSize(1200, 840));
    spin(kQuiet);
    check(guest.offers.isEmpty() && settings.string_value(K::kDesktopRestore).isEmpty(),
          "without the resolution to return to nothing is changed");
    guest.agent = true;
    desktop.guest_ready();
    spin(kQuiet);
    check(guest.now == (DisplayMode{1200, 840}), "the size is applied once the guest answers");
    guest.agent = false;
    desktop.closed();
    spin(kQuiet);
    check(guest.offers.last() == kConfiguredProperty &&
              settings.string_value(K::kDesktopRestore) == QStringLiteral("2560x1440"),
          "a close the guest's agent does not answer puts the modes back and keeps what to return "
          "to");
    guest.agent = true;
    guest.now = DisplayMode{1920, 1080};
    desktop.guest_ready();
    spin(kQuiet);
    check(guest.now == (DisplayMode{2560, 1440}) &&
              settings.string_value(K::kDesktopRestore).isEmpty(),
          "and the resolution follows when the agent is back");

    guest.running = false;
    desktop.opened();
    desktop.resized(QSize(1000, 700));
    spin(kQuiet);
    check(!desktop.applied().has_value(), "a device that takes no modes leaves nothing applied");
    desktop.closed();
    spin(kQuiet);
}

void the_setting_is_followed_while_open(ManagerSettings& settings) {
    reset(settings, false);
    FakeGuest guest;
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, kSettle);
    desktop.opened();
    desktop.resized(QSize(1200, 840));
    spin(kQuiet);
    check(guest.offers.isEmpty(), "off: nothing");
    settings.set_value(K::kOneToOneDesktop, true);
    spin(kQuiet);
    check(guest.offers == QStringList{QStringLiteral("1200x840")} &&
              guest.now == (DisplayMode{1200, 840}),
          "turning the setting on with the desktop open applies the window's size at once");
    settings.set_value(K::kOneToOneDesktop, false);
    spin(kQuiet);
    check(guest.offers.last() == kConfiguredProperty && guest.now == (DisplayMode{2560, 1440}) &&
              settings.string_value(K::kDesktopRestore).isEmpty(),
          "turning it off with the desktop open restores at once");
    desktop.resized(QSize(1400, 900));
    spin(kQuiet);
    check(guest.offers.size() == 2, "and later sizes are not followed");
    desktop.closed();
    spin(kQuiet);
    check(guest.offers.size() == 2, "nor is anything restored twice");
}

void a_quick_reopen_ends_applied(ManagerSettings& settings) {
    reset(settings, true);
    FakeGuest guest;
    OneToOneDesktop desktop(settings, guest.hooks(), nullptr, 0);
    desktop.opened();
    desktop.resized(QSize(1200, 840));
    spin(kQuiet);
    desktop.closed();
    desktop.opened();
    desktop.resized(QSize(1300, 900));
    spin(kQuiet);
    check(guest.now == (DisplayMode{1300, 900}) && desktop.applied().has_value() &&
              settings.string_value(K::kDesktopRestore) == QStringLiteral("2560x1440"),
          "closing and reopening at once ends at the new window's size, still knowing what to "
          "return to");
    desktop.closed();
    spin(kQuiet);
    check(guest.now == (DisplayMode{2560, 1440}) &&
              settings.string_value(K::kDesktopRestore).isEmpty(),
          "and the close after it restores");
}

}  // namespace

void a_switch_carries_the_scale_windows_had() {
    const nlohmann::json plain = OneToOneDesktop::request(DisplayMode{1412, 757}, std::nullopt);
    check(plain == nlohmann::json({{"width", 1412}, {"height", 757}}),
          "a switch with no scale known asks for the size alone");
    const nlohmann::json scaled = OneToOneDesktop::request(DisplayMode{1412, 757}, 125);
    check(scaled == nlohmann::json({{"width", 1412}, {"height", 757}, {"scaling_percent", 125}}),
          "a switch asks for the scale Windows was given, since a new resolution starts at 100 %");
    check(OneToOneDesktop::request(std::nullopt, 125) == nlohmann::json::object(),
          "asking what the resolution is changes neither");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    check(DisplayModes::clamped(DisplayMode{1201, 841}) == (DisplayMode{1201, 841}),
          "a size the display can offer is kept to the pixel, odd sides too");
    check(DisplayModes::clamped(DisplayMode{99, 8193}) == (DisplayMode{100, 8192}) &&
              DisplayModes::refusal(DisplayModes::clamped(DisplayMode{20000, 1})).isEmpty(),
          "a clamped size is one the display accepts");
    check(!ManagerSettings::default_value(K::kOneToOneDesktop).toBool() &&
              !ManagerSettings::needs_restart(K::kOneToOneDesktop),
          "1:1 desktop is off unless turned on, and needs no restart");

    ManagerSettings settings;
    off_changes_nothing(settings);
    a_switch_carries_the_scale_windows_had();
    follows_the_window_and_restores(settings);
    keeps_a_resolution_that_is_not_preferred(settings);
    a_window_already_that_size_changes_nothing(settings);
    shutdown_restores(settings);
    a_stale_state_is_restored(settings);
    an_unreachable_guest_is_restored_later(settings);
    the_setting_is_followed_while_open(settings);
    a_quick_reopen_ends_applied(settings);

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
