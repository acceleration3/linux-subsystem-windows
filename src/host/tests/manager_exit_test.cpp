#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/manager/manager_exit.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <chrono>
#include <cstdio>

using lsw::host::config::ManagerSettings;
using lsw::host::manager::ManagerExit;
using K = ManagerSettings::Key;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

struct Machine {
    bool tray = true;
    bool guest_running = true;
    bool guest_obeys = true;
    int shutdown_requests = 0;
};

struct Seen {
    int hides = 0;
    int lefts = 0;
    int quits = 0;
    QList<bool> tray;
};

struct Case {
    Machine machine;
    Seen seen;
    ManagerExit exit;

    Case(ManagerSettings& settings, bool to_tray, bool shut_guest, Machine start = {},
         std::chrono::milliseconds wait = std::chrono::milliseconds{5000})
        : machine(start), exit((settings.set_value(K::kCloseToTray, to_tray),
                                settings.set_value(K::kShutGuestOnQuit, shut_guest), settings),
                               ManagerExit::Host{[this] { return machine.tray; },
                                                 [this] { return machine.guest_running; },
                                                 [this] { ++machine.shutdown_requests; }},
                               nullptr, wait, std::chrono::milliseconds{10}) {
        QObject::connect(&exit, &ManagerExit::hide_requested, [this] { ++seen.hides; });
        QObject::connect(&exit, &ManagerExit::left, [this] { ++seen.lefts; });
        QObject::connect(&exit, &ManagerExit::quit_requested, [this] { ++seen.quits; });
        QObject::connect(&exit, &ManagerExit::tray_wanted,
                         [this](bool wanted) { seen.tray << wanted; });
    }
};

void spin(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

}  // namespace

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QCoreApplication app(argc, argv);
    ManagerSettings settings;

    check(ManagerSettings::default_value(K::kCloseToTray).toBool(),
          "the manager stays in the tray unless that is turned off");
    check(ManagerSettings::default_value(K::kShutGuestOnQuit).toBool(),
          "quitting shuts the guest down unless the setting is turned off");
    check(!ManagerSettings::needs_restart(K::kShutGuestOnQuit), "the quit setting applies at once");

    {
        Machine machine;
        int quits = 0;
        ManagerExit untouched{
            settings,
            ManagerExit::Host{[&] { return machine.tray; }, [&] { return machine.guest_running; },
                              [&] { ++machine.shutdown_requests; }},
            nullptr, std::chrono::milliseconds{150}, std::chrono::milliseconds{10}};
        QObject::connect(&untouched, &ManagerExit::quit_requested, [&] { ++quits; });
        untouched.window_closed();
        check(
            quits == 0 && machine.shutdown_requests == 0 && !untouched.leaving(),
            "with the settings untouched, closing the window hides it and asks the guest nothing");
        untouched.quit();
        check(machine.shutdown_requests == 1 && quits == 0,
              "with the settings untouched, quitting asks the guest to shut down and waits");
        spin(60);
        check(quits == 0, "for a guest that is still shutting down");
        spin(400);
        check(quits == 1 && machine.shutdown_requests == 1,
              "no longer than the bound, having asked once");
    }
    {
        Case c{settings, false, true};
        c.exit.hand_over();
        check(c.exit.finished() && c.seen.lefts == 1 && c.seen.quits == 0,
              "handing over to another manager process takes the window and the tray icon away");
        c.exit.window_closed();
        c.exit.quit();
        spin(40);
        check(c.machine.shutdown_requests == 0 && c.seen.quits == 0,
              "and leaves the guest alone, also when the windows are closed for the exit");
    }
    {
        Case c{settings, true, true};
        c.exit.quit();
        c.exit.hand_over();
        check(c.machine.shutdown_requests == 1 && !c.exit.finished(),
              "a hand-over does not cut a quit that is already waiting for the guest");
    }
    {
        Case c{settings, true, false};
        check(c.exit.stays_in_tray(), "with the option on and a tray there is a tray icon");
        c.exit.window_closed();
        spin(50);
        check(c.seen.hides == 1, "closing the window with the option on hides it");
        check(c.seen.quits == 0 && c.seen.lefts == 0 && !c.exit.leaving(),
              "closing the window with the option on does not quit the manager");
        check(c.machine.shutdown_requests == 0, "hiding to the tray leaves the guest alone");
        c.exit.window_closed();
        check(c.seen.hides == 2 && c.seen.quits == 0, "a second close hides again");
    }
    {
        Case c{settings, true, true};
        c.exit.window_closed();
        spin(50);
        check(c.seen.hides == 1 && c.seen.quits == 0 && c.machine.shutdown_requests == 0,
              "hiding to the tray leaves the guest alone also when quitting would shut it down");
    }
    {
        Case c{settings, false, false};
        check(!c.exit.stays_in_tray(), "with the option off there is no tray icon");
        c.exit.window_closed();
        check(c.seen.quits == 1 && c.seen.hides == 0,
              "closing the window with the option off quits the manager");
        check(c.seen.lefts == 1, "quitting takes the window and the tray icon away");
        check(c.exit.finished(), "after quitting the window's close goes through");
        check(c.machine.shutdown_requests == 0,
              "quitting by closing the window leaves the guest alone with the quit setting off");
        c.exit.window_closed();
        c.exit.quit();
        check(c.seen.quits == 1 && c.seen.lefts == 1, "quitting happens once");
    }
    {
        Machine no_tray;
        no_tray.tray = false;
        Case c{settings, true, false, no_tray};
        check(!c.exit.stays_in_tray(), "without a system tray there is nothing to stay in");
        c.exit.window_closed();
        check(c.seen.quits == 1 && c.seen.hides == 0,
              "closing the window quits when the desktop has no system tray");
    }
    {
        Case c{settings, true, false};
        settings.set_value(K::kCloseToTray, false);
        check(c.seen.tray == QList<bool>{false}, "turning the option off takes the tray icon away");
        c.exit.window_closed();
        check(c.seen.quits == 1, "and closing the window then quits");
    }
    {
        Case c{settings, false, false};
        settings.set_value(K::kCloseToTray, true);
        check(c.seen.tray == QList<bool>{true}, "turning the option on brings the tray icon");
        c.exit.window_closed();
        check(c.seen.quits == 0 && c.seen.hides == 1, "and closing the window then hides it");
    }
    {
        Case c{settings, true, false};
        c.exit.quit();
        check(c.seen.quits == 1 && c.seen.lefts == 1, "Quit from the tray quits at once");
        check(c.machine.shutdown_requests == 0,
              "Quit from the tray leaves the guest alone with the quit setting off");
    }
    {
        Case c{settings, true, true};
        c.exit.quit();
        check(c.machine.shutdown_requests == 1,
              "with the quit setting on, quitting asks the guest to shut down");
        check(c.seen.lefts == 1, "the window and the tray icon go away while the guest shuts down");
        check(c.seen.quits == 0 && c.exit.leaving() && !c.exit.finished(),
              "the manager waits for the guest");
        c.exit.quit();
        c.exit.window_closed();
        spin(60);
        check(c.machine.shutdown_requests == 1 && c.seen.quits == 0 && c.seen.hides == 0,
              "quitting again while waiting asks nothing more");
        c.machine.guest_running = false;
        spin(60);
        check(c.seen.quits == 1, "the manager quits once the guest is off");
        check(c.machine.shutdown_requests == 1 && c.seen.lefts == 1,
              "the guest was asked to shut down exactly once");
        spin(60);
        check(c.seen.quits == 1, "and it quits once");
    }
    {
        Case c{settings, false, true};
        c.exit.window_closed();
        check(c.machine.shutdown_requests == 1 && c.seen.quits == 0,
              "closing the window with the tray option off shuts the guest down too");
        c.machine.guest_running = false;
        spin(60);
        check(c.seen.quits == 1, "and quits when it is off");
    }
    {
        Case c{settings, true, true, Machine{}, std::chrono::milliseconds{150}};
        QElapsedTimer clock;
        clock.start();
        c.exit.quit();
        spin(60);
        check(c.seen.quits == 0, "a guest that is slow to stop is waited for");
        spin(400);
        check(c.seen.quits == 1, "the wait for the guest is bounded: the manager quits anyway");
        check(c.machine.shutdown_requests == 1, "a slow guest is still asked only once");
    }
    {
        Machine stopped;
        stopped.guest_running = false;
        Case c{settings, true, true, stopped};
        c.exit.quit();
        check(c.seen.quits == 1 && c.machine.shutdown_requests == 0,
              "with no guest running, quitting asks nothing and quits at once");
    }

    std::printf("%s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
