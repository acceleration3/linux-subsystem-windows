#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mirror/guest_mirrors.hpp"
#include "lsw/host/mirror/window_mirror.hpp"
#include "lsw/linkproto/envelope.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QList>
#include <QPointer>
#include <QProcess>
#include <QSize>
#include <QSocketNotifier>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <unistd.h>

namespace {
int failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok)
        ++failures;
}

bool wait_for(const std::function<bool()>& done, int timeout_ms = 4000) {
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    poll.start(5);
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    if (!done())
        loop.exec();
    return done();
}

int fake_mirror(QCoreApplication& app) {
    const auto mode = qgetenv("LSW_TEST_MIRROR_MODE");
    if (mode == "mismatch")
        return 3;
    if (mode == "early-exit")
        return 0;
    if (mode == "invalid-port") {
        std::puts("LISTENING 99999");
        std::fflush(stdout);
    } else {
        const QByteArray frame_host_claim = qgetenv("LSW_TEST_FRAME_HOST_CLAIM");
        if (!frame_host_claim.isEmpty() && !app.arguments().contains(QStringLiteral("--aumid"))) {
            std::printf("APP %s\n", frame_host_claim.constData());
        }
        std::printf("LIST");
        std::fflush(stdout);
        QTimer::singleShot(10, &app, [] {
            std::puts("ENING 43210");
            std::fflush(stdout);
        });
    }
    QTimer frames;
    QObject::connect(&frames, &QTimer::timeout, &app, [&] {
        if (!QFile::exists(qEnvironmentVariable("LSW_TEST_FRAME")))
            return;
        frames.stop();
        std::puts("READY");
        if (app.arguments().contains(QStringLiteral("--desktop"))) {
            std::puts("SIZE 0 840");
            std::puts("SIZE wide");
            std::puts("SIZE 1201 841");
        }
        std::fflush(stdout);
    });
    frames.start(10);
    QSocketNotifier stdin_ready(STDIN_FILENO, QSocketNotifier::Read);
    QByteArray commands;
    QObject::connect(&stdin_ready, &QSocketNotifier::activated, &app, [&] {
        char bytes[128];
        const auto count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count <= 0) {
            app.quit();
            return;
        }
        commands.append(bytes, count);
        if (commands.contains("RAISE\n")) {
            QFile marker(qEnvironmentVariable("LSW_TEST_RAISED"));
            if (marker.open(QIODevice::WriteOnly))
                marker.write("raised");
            commands.clear();
        }
    });
    return app.exec();
}

int live_smoke(QCoreApplication& app) {
    lsw::host::link::AgentRpcClient rpc;
    rpc.set_target(1, 5102);
    const auto before = rpc.request("list_windows");
    check(before.ok, "live guest answers window enumeration");
    if (!before.ok)
        return 1;
    const std::string path = "C:\\Windows\\System32\\charmap.exe";
    for (const auto& window : before.body.value("windows", nlohmann::json::array())) {
        if (QString::fromStdString(window.value("exe_path", std::string{}))
                .compare(QString::fromStdString(path), Qt::CaseInsensitive) == 0) {
            std::puts(
                "Refusing to touch an existing Character Map window; close it before --live.");
            return 1;
        }
    }
    lsw::host::apps::AppConfig config;
    config.id = QStringLiteral("lsw-smoke-charmap");
    config.path = QString::fromStdString(path);
    quint32 original_pid = 0;
    for (int pass = 0; pass != 2; ++pass) {
        lsw::host::mirror::GuestMirrors mirrors(rpc);
        lsw::host::apps::GuestAppLauncher launcher(rpc, mirrors);
        int opened = 0;
        QString error;
        quint32 pid = 0;
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::launched, &app,
                         [&](const QString&, quint32 launched_pid) {
                             pid = launched_pid;
                             ++opened;
                         });
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::failed, &app,
                         [&](const QString&, const QString& why) { error = why; });
        launcher.launch(config, QString());
        check(wait_for([&] { return opened || !error.isEmpty(); }, 30000),
              "live mirror startup completes");
        if (!error.isEmpty())
            std::fprintf(stderr, "%s\n", error.toUtf8().constData());
        check(error.isEmpty() && launcher.running(config),
              "live guest frame makes the mirror ready");
        if (!error.isEmpty() || pid == 0)
            break;
        if (pass == 0)
            original_pid = pid;
        else
            check(pid == original_pid,
                  "reattach after host mirror closes reuses the Windows process");
        launcher.launch(config, QString());
        check(wait_for([&] { return opened == 2; }) && pid == original_pid,
              "repeated open raises the existing live mirror");
        check(mirrors.findChildren<lsw::host::mirror::WindowMirror*>().size() == 1,
              "only one live mirror exists per application");
        const auto again = rpc.request("launch", {{"app_id", path}, {"reuse_existing", true}});
        check(again.ok && again.body.value("pid", quint32{0}) == original_pid,
              "guest-side repeated open returns the existing process id");
        if (pass == 0) {
            const auto windows = rpc.request("list_windows");
            for (const auto& window : windows.body.value("windows", nlohmann::json::array())) {
                if (window.value("pid", quint32{0}) != original_pid)
                    continue;
                const auto minimized = rpc.request(
                    "window_command", {{"hwnd", window["hwnd"]}, {"command", "minimize"}});
                check(minimized.ok, "minimize the smoke test's guest window");
                const auto is_minimized = [&] {
                    const auto listed = rpc.request("list_windows");
                    for (const auto& candidate :
                         listed.body.value("windows", nlohmann::json::array())) {
                        if (candidate.value("pid", quint32{0}) == original_pid)
                            return candidate.value("minimized", false);
                    }
                    return false;
                };
                check(wait_for(is_minimized), "guest minimization takes effect");
                launcher.launch(config, QString());
                check(wait_for([&] { return !is_minimized(); }),
                      "open again restores the minimized guest app");
                break;
            }
        }
    }
    const auto windows = rpc.request("list_windows");
    if (windows.ok && original_pid != 0) {
        for (const auto& window : windows.body.value("windows", nlohmann::json::array())) {
            if (window.value("exe_path", std::string{}) != path)
                continue;
            const auto closed = rpc.request("close_window", {{"hwnd", window["hwnd"]}});
            check(closed.ok, "close only the smoke test's own guest window");
        }
    }
    QThreadPool::globalInstance()->waitForDone();
    return failures == 0 ? 0 : 1;
}

int live_package_smoke(QCoreApplication& app, const QString& path) {
    if (path.isEmpty()) {
        std::fprintf(stderr, "--live-package requires a package path already in the guest\n");
        return 1;
    }
    lsw::host::link::AgentRpcClient rpc;
    rpc.set_target(1, 5102);
    {
        lsw::host::mirror::GuestMirrors mirrors(rpc);
        lsw::host::apps::GuestAppLauncher launcher(rpc, mirrors);
        lsw::host::apps::AppConfig config;
        config.path = path;
        quint32 pid = 0;
        QString error;
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::launched, &app,
                         [&](const QString&, quint32 value) { pid = value; });
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::failed, &app,
                         [&](const QString&, const QString& why) { error = why; });
        launcher.launch(config, QString());
        check(wait_for([&] { return pid != 0 || !error.isEmpty(); }, 30000),
              "live package activation and first installer frame complete");
        if (!error.isEmpty())
            std::fprintf(stderr, "%s\n", error.toUtf8().constData());
        check(error.isEmpty() && pid != 0 && launcher.running(config),
              "live App Installer window is mirrored");
        const auto processes = mirrors.findChildren<QProcess*>();
        check(processes.size() == 1, "one application mirror owns the installer windows");
        if (processes.size() == 1) {
            const auto arguments = processes.first()->arguments();
            const auto index = arguments.indexOf(QStringLiteral("--pid"));
            check(!arguments.contains(QStringLiteral("--desktop")) && index >= 0 &&
                      index + 1 < arguments.size() && arguments[index + 1] == QString::number(pid),
                  "real mirror runs in application mode for the discovered installer PID");
        }
        if (pid != 0) {
            const auto listed = rpc.request("list_windows");
            bool actual_installer = false;
            for (const auto& window : listed.body.value("windows", nlohmann::json::array())) {
                const auto image = QString::fromStdString(window.value("exe_path", std::string{}));
                if (window.value("pid", quint32{0}) == pid &&
                    image.endsWith(QStringLiteral("\\AppInstaller.exe"), Qt::CaseInsensitive)) {
                    actual_installer = true;
                    std::printf("App Installer PID %u: %s (%dx%d)\n", pid,
                                window.value("title", std::string{}).c_str(),
                                window.value("client_w", 0), window.value("client_h", 0));
                }
            }
            check(listed.ok && actual_installer,
                  "mirror root is the visible App Installer, not Explorer or an activation broker");
        }
    }
    QThreadPool::globalInstance()->waitForDone();
    return failures == 0 ? 0 : 1;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--managed")))
        return fake_mirror(app);
    if (app.arguments().contains(QStringLiteral("--live")))
        return live_smoke(app);
    if (const auto index = app.arguments().indexOf(QStringLiteral("--live-package")); index >= 0)
        return live_package_smoke(app, app.arguments().value(index + 1));
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory available");
    const QString frame = directory.filePath("frame");
    const QString raised = directory.filePath("raised");
    qputenv("LSW_REMOTEWIN", QCoreApplication::applicationFilePath().toUtf8());
    qputenv("LSW_TEST_FRAME", frame.toUtf8());
    qputenv("LSW_TEST_RAISED", raised.toUtf8());

    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "fake agent listening");
    int launches = 0;
    int mirror_starts = 0;
    int packages = 0;
    quint32 package_pid = 789;
    quint32 launch_pid = 123;
    int desktops = 0;
    bool defer_launch = false;
    std::function<void()> deferred_launch;
    nlohmann::json guest_windows = nlohmann::json::array();
    quint32 mirror_pid = 0;
    bool desktop_mirror = false;
    bool reject_mirror = false;
    bool reject_launch = false;
    QList<QPointer<QTcpSocket>> agent_sockets;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        auto* socket = server.nextPendingConnection();
        agent_sockets.append(socket);
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
            *buffer += socket->readAll();
            while (buffer->size() >= 5) {
                const auto size = lsw::linkproto::read_payload_len(
                    reinterpret_cast<const std::uint8_t*>(buffer->constData()));
                if (buffer->size() < 5 + size)
                    break;
                const auto message = nlohmann::json::parse(buffer->mid(5, size).toStdString());
                buffer->remove(0, 5 + size);
                nlohmann::json body;
                if (message["cmd"] == "launch") {
                    ++launches;
                    check(message["params"].value("reuse_existing", false),
                          "UI asks to reuse an existing guest app");
                    body = reject_launch
                               ? nlohmann::json{{"ok", false}, {"error", "activation refused"}}
                               : nlohmann::json{{"ok", true}, {"pid", launch_pid}, {"pid_created", 777}, {"job", "test-job"}};
                } else if (message["cmd"] == "open_package") {
                    ++packages;
                    check(message["params"]["path"] == "C:\\test.MSIX",
                          "package path passed unchanged to Windows shell");
                    check(!message["params"].contains("args"),
                          "package activation has no executable arguments");
                    body = reject_launch
                               ? nlohmann::json{{"ok", false},
                                                {"error", "App Installer is not installed"}}
                               : nlohmann::json{{"ok", true}, {"pid", package_pid}};
                } else if (message["cmd"] == "launch_store_app") {
                    body = nlohmann::json{{"ok", true}, {"pid", launch_pid}, {"pid_created", 777}};
                } else if (message["cmd"] == "list_windows") {
                    body = nlohmann::json{{"windows", guest_windows}};
                } else {
                    ++mirror_starts;
                    desktop_mirror = message["params"].value("desktop", false);
                    if (desktop_mirror)
                        ++desktops;
                    mirror_pid = message["params"].value("pid", quint32{0});
                    check(message["params"]["port"] == 43210, "guest gets the actual bound port");
                    body = reject_mirror
                               ? nlohmann::json{{"ok", false}, {"error", "missing guest mirror"}}
                               : nlohmann::json{{"ok", true}, {"pid", 456}};
                }
                const auto send = [socket, reply = nlohmann::json{{"id", message["id"]}, {"result", body}}.dump()] {
                    std::uint8_t header[5];
                    lsw::linkproto::write_header(header, lsw::linkproto::FrameType::Control,
                                                 static_cast<std::uint32_t>(reply.size()));
                    socket->write(reinterpret_cast<const char*>(header), 5);
                    socket->write(reply.data(), static_cast<qint64>(reply.size()));
                };
                if (defer_launch && message["cmd"] == "launch")
                    deferred_launch = send;
                else
                    send();
            }
        });
    });

    const auto push_event = [&](const nlohmann::json& message) {
        const auto event = message.dump();
        std::uint8_t header[5];
        lsw::linkproto::write_header(header, lsw::linkproto::FrameType::Control,
                                     static_cast<std::uint32_t>(event.size()));
        for (const auto& socket : agent_sockets) {
            if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                socket->write(reinterpret_cast<const char*>(header), 5);
                socket->write(event.data(), static_cast<qint64>(event.size()));
                socket->flush();
            }
        }
    };

    lsw::host::link::AgentRpcClient rpc;
    rpc.set_target(1, server.serverPort());
    {
        lsw::host::mirror::GuestMirrors mirrors(rpc);
        lsw::host::apps::GuestAppLauncher launcher(rpc, mirrors);
        lsw::host::apps::AppConfig config;
        config.id = QStringLiteral("test-app");
        config.path = QStringLiteral("C:\\test.exe");
        int opened = 0;
        int errors = 0;
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::launched, &app,
                         [&](const QString&, quint32) { ++opened; });
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::failed, &app,
                         [&](const QString&, const QString&) { ++errors; });
        launcher.launch(config, QString());
        launcher.launch(config, QString());
        check(launcher.opening(config), "launch immediately shows opening state");
        check(wait_for([&] { return mirror_starts == 1; }),
              "mirror startup does not block the event loop");
        check(launches == 1 && opened == 0,
              "duplicate launch coalesced; no success before a frame");
        QFile marker(frame);
        check(marker.open(QIODevice::WriteOnly), "release first frame");
        marker.close();
        check(wait_for([&] { return opened == 1; }), "first frame reports launch success");
        check(launcher.running(config) && !launcher.opening(config),
              "running state follows readiness");
        launcher.launch(config, QString());
        check(wait_for([&] { return QFile::exists(raised); }),
              "open again sends RAISE to the existing mirror");
        check(launches == 2 && mirror_starts == 1 && errors == 0,
              "open again asks Windows to reactivate but reuses the host mirror");
        reject_launch = true;
        launcher.launch(config, QString());
        check(wait_for([&] { return errors == 1; }), "reactivation failure reaches the caller");
        check(launcher.running(config) && !launcher.opening(config),
              "reactivation failure preserves the existing mirror");
        reject_launch = false;
        const int before = launches;
        config.cmdline = QStringLiteral("--profile test");
        launcher.launch(config, QString());
        check(launches == before && mirror_starts == 1,
              "configured arguments are not executed again for an existing mirror");
        config.cmdline.clear();
        for (auto* mirror : mirrors.findChildren<lsw::host::mirror::WindowMirror*>())
            delete mirror;
        check(!launcher.running(config), "destroyed mirror is not treated as running");
        reject_mirror = true;
        launcher.launch(config, QString());
        check(wait_for([&] { return errors == 2; }), "guest startup failure reaches the caller");
        check(!launcher.opening(config) && !launcher.running(config),
              "failed startup clears state for retry");
    }
    reject_mirror = false;
    {
        lsw::host::mirror::GuestMirrors mirrors(rpc);
        lsw::host::apps::GuestAppLauncher launcher(rpc, mirrors);
        lsw::host::apps::AppConfig config;
        config.path = QStringLiteral("C:\\test.MSIX");
        int opened = 0;
        int errors = 0;
        quint32 pid = 99;
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::launched, &app,
                         [&](const QString&, quint32 value) {
                             ++opened;
                             pid = value;
                         });
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::failed, &app,
                         [&](const QString&, const QString&) { ++errors; });
        const int before = launches;
        launcher.launch(config, QString());
        check(wait_for([&] { return opened == 1 || errors; }),
              "package opens using the actual App Installer window PID");
        check(packages == 1 && launches == before && errors == 0 && pid == 789,
              "MSIX uses shell activation, not CreateProcess");
        check(!desktop_mirror && mirror_pid == 789 && launcher.running(config),
              "only the App Installer application is mirrored, never the desktop");
        launcher.launch(config, QString());
        check(wait_for([&] { return opened == 2; }),
              "reopen package reuses and raises the installer application mirror");
        reject_launch = true;
        launcher.launch(config, QString());
        check(wait_for([&] { return errors == 1; }), "missing App Installer error reaches caller");
        reject_launch = false;
        package_pid = 0;
        const int before_mirrors = mirror_starts;
        launcher.launch(config, QString());
        check(wait_for([&] { return errors == 2; }),
              "old agent with no installer PID reports an error");
        check(mirror_starts == before_mirrors && !desktop_mirror,
              "missing package PID cannot fall back to a desktop mirror");
        package_pid = 789;
        config.path = QStringLiteral("C:\\args.msix");
        config.cmdline = QStringLiteral("--silent");
        const int before_packages = packages;
        launcher.launch(config, QString());
        check(errors == 3 && packages == before_packages,
              "MSIX arguments rejected instead of ignored");
    }
    QFile::remove(frame);
    {
        const auto push_ended = [&](quint32 pid, quint64 created) {
            push_event({{"event", "launch_ended"}, {"pid", pid}, {"created", created}});
        };
        lsw::host::mirror::GuestMirrors mirrors(rpc);
        lsw::host::apps::GuestAppLauncher launcher(rpc, mirrors);
        lsw::host::apps::AppConfig config;
        config.id = QStringLiteral("ended-app");
        config.path = QStringLiteral("C:\\ended.exe");
        const int before = mirror_starts;
        launcher.launch(config, QString());
        check(wait_for([&] { return mirror_starts == before + 1; }), "launch under test starts a mirror");
        push_ended(123, 555);
        push_ended(124, 777);
        wait_for([] { return false; }, 300);
        check(launcher.opening(config),
              "an ended report of a reused pid or another creation time does not end the launch");
        push_ended(123, 777);
        check(wait_for([&] { return !launcher.opening(config); }),
              "an ended report matching pid and creation time ends the launch");
    }
    {
        QFile marker(frame);
        check(marker.open(QIODevice::WriteOnly), "frames flow for the exclusivity scenario");
        marker.close();
        const auto window = [](quint32 pid, quint32 root_pid, bool caption, const char* image) {
            return nlohmann::json{{"hwnd", pid * 10},   {"owner", 0},          {"pid", pid},
                                  {"root_pid", root_pid}, {"has_caption", caption}, {"exe_path", image},
                                  {"title", "window"},  {"client_w", 640},     {"client_h", 480},
                                  {"screen_x", 100},    {"screen_y", 100},     {"minimized", false},
                                  {"on_screen", true}};
        };
        const auto appeared = [&](nlohmann::json row) {
            row["event"] = "window_appeared";
            push_event(row);
        };
        lsw::host::mirror::GuestMirrors mirrors(rpc);
        lsw::host::apps::GuestAppLauncher launcher(rpc, mirrors);
        const auto mirrored = [&] {
            QList<quint32> pids;
            for (const auto* process : mirrors.findChildren<QProcess*>()) {
                const auto arguments = process->arguments();
                const auto index = arguments.indexOf(QStringLiteral("--pid"));
                if (process->state() != QProcess::NotRunning && index >= 0)
                    pids << arguments.value(index + 1).toUInt();
            }
            std::sort(pids.begin(), pids.end());
            return pids;
        };
        const auto close_desktop = [&] {
            for (auto* process : mirrors.findChildren<QProcess*>()) {
                if (process->arguments().contains(QStringLiteral("--desktop")))
                    process->closeWriteChannel();
            }
        };
        const auto settle = [] { wait_for([] { return false; }, 300); };
        int opened = 0;
        QObject::connect(&launcher, &lsw::host::apps::GuestAppLauncher::launched, &app,
                         [&](const QString&, quint32) { ++opened; });
        lsw::host::apps::AppConfig first;
        first.id = QStringLiteral("exclusive-first");
        first.path = QStringLiteral("C:\\first.exe");
        lsw::host::apps::AppConfig second;
        second.id = QStringLiteral("exclusive-second");
        second.path = QStringLiteral("C:\\second.exe");
        lsw::host::apps::AppConfig third;
        third.id = QStringLiteral("exclusive-third");
        third.path = QStringLiteral("C:\\third.exe");

        launcher.launch(first, QString());
        check(wait_for([&] { return opened == 1; }) && mirrored() == QList<quint32>{123},
              "a launched application has its own mirror while no desktop is open");

        int desktop_opens = 0;
        int desktop_closes = 0;
        QList<QSize> desktop_rooms;
        QObject::connect(&mirrors, &lsw::host::mirror::GuestMirrors::desktop_opened, &app,
                         [&] { ++desktop_opens; });
        QObject::connect(&mirrors, &lsw::host::mirror::GuestMirrors::desktop_closed, &app,
                         [&] { ++desktop_closes; });
        QObject::connect(&mirrors, &lsw::host::mirror::GuestMirrors::desktop_resized, &app,
                         [&](const QSize& pixels) { desktop_rooms << pixels; });
        const int desktops_before = desktops;
        mirrors.open_desktop(QStringLiteral("test desktop"));
        check(wait_for([&] { return desktops == desktops_before + 1; }) && mirrors.desktop_open(),
              "the desktop mirror opens");
        check(desktop_opens == 1 && desktop_closes == 0, "and says so once");
        check(wait_for([&] { return !desktop_rooms.isEmpty(); }) &&
                  desktop_rooms == QList<QSize>{QSize(1201, 841)},
              "the size the desktop window settles on is passed on in pixels, and a malformed one is not");
        check(mirrored().isEmpty(), "opening the desktop closes every application mirror");
        check(launcher.running(first) && !launcher.opening(first),
              "an application the desktop shows is still running");

        appeared(window(900, 0, true, "C:\\installer.exe"));
        settle();
        check(mirrored().isEmpty(), "a window appearing while the desktop is open starts no mirror");

        launch_pid = 321;
        launcher.launch(second, QString());
        check(wait_for([&] { return opened == 2; }) && launcher.running(second) && !launcher.opening(second),
              "a launch while the desktop is open completes without waiting for a mirror");
        check(mirrored().isEmpty(), "a launch while the desktop is open starts no application mirror");

        auto parked = window(60, 0, true, "C:\\Windows\\System32\\svchost.exe");
        parked["on_screen"] = false;
        parked["client_w"] = 20;
        parked["client_h"] = 20;
        parked["screen_x"] = -40000;
        parked["screen_y"] = -40000;
        auto tiny = window(61, 0, true, "C:\\tiny.exe");
        tiny["client_w"] = 20;
        tiny["client_h"] = 20;
        auto owned = window(70, 0, true, "C:\\dialog.exe");
        owned["owner"] = 5;
        auto minimized = window(80, 0, true, "C:\\minimized.exe");
        minimized["minimized"] = true;
        minimized["on_screen"] = false;
        minimized["client_w"] = 160;
        minimized["client_h"] = 28;
        auto another = window(900, 0, true, "C:\\installer.exe");
        another["hwnd"] = 9001;
        guest_windows = {window(500, 123, true, "C:\\first-helper.exe"),
                         window(321, 321, false, "C:\\second.exe"),
                         window(900, 0, true, "C:\\installer.exe"),
                         another,
                         window(50, 0, false, "C:\\Windows\\explorer.exe"),
                         parked,
                         tiny,
                         owned,
                         minimized};
        close_desktop();
        check(wait_for([&] { return mirrored() == QList<quint32>{80, 123, 321, 900}; }),
              "closing the desktop mirrors each launched application that shows a window, captioned or "
              "not, and one mirror per other process with a captioned window on screen");
        settle();
        check(mirrored() == QList<quint32>{80, 123, 321, 900},
              "uncaptioned, owned, off-screen and degenerate windows get no mirror of their own");
        check(desktop_closes == 1 && desktop_opens == 1, "the desktop closing is said once");
        check(!mirrors.desktop_open() && launcher.running(first) && launcher.running(second) &&
                  !launcher.opening(first) && !launcher.opening(second),
              "after the desktop closes the launched applications run under their own identity");
        check(opened == 2, "mirroring an application again reports no new launch");

        appeared(window(950, 0, true, "C:\\AppInstaller.exe"));
        check(wait_for([&] { return mirrored().contains(950); }),
              "an unowned captioned window appearing with no desktop open gets a mirror");
        appeared(window(950, 0, true, "C:\\AppInstaller.exe"));
        appeared(window(951, 0, false, "C:\\Windows\\explorer.exe"));
        appeared(parked);
        appeared(owned);
        appeared(window(501, 123, true, "C:\\first-helper.exe"));
        settle();
        check(mirrored() == QList<quint32>{80, 123, 321, 900, 950},
              "a second window of a mirrored process and windows not worth a mirror start none");

        launch_pid = 555;
        defer_launch = true;
        const int launches_before = launches;
        launcher.launch(third, QString());
        check(wait_for([&] { return launches == launches_before + 1 && deferred_launch; }),
              "a launch is in flight");
        appeared(window(555, 0, true, "C:\\third.exe"));
        settle();
        check(!mirrored().contains(555), "a window appearing during a launch waits for the launch");
        defer_launch = false;
        deferred_launch();
        check(wait_for([&] { return opened == 3; }) && mirrored().count(555) == 1 && launcher.running(third),
              "the window of a launch in flight is mirrored once, under the launched application");

        lsw::host::apps::AppConfig store;
        store.id = QStringLiteral("exclusive-store");
        store.aumid = QStringLiteral("Vendor.App_abc!App");
        launch_pid = 666;
        launcher.launch(store, QString());
        check(wait_for([&] { return opened == 4; }) && mirrored().contains(666), "a store application is mirrored");
        qputenv("LSW_TEST_FRAME_HOST_CLAIM", store.aumid.toUtf8());
        appeared(window(667, 0, true, "C:\\Windows\\System32\\ApplicationFrameHost.exe"));
        check(wait_for([&] { return mirrored().contains(667); }),
              "the frame host of a store application starts a mirror when its window appears");
        check(wait_for([&] { return !mirrored().contains(667); }) && mirrored().contains(666) &&
                  launcher.running(store),
              "a mirror that turns out to show a launched store application's windows gives way to that application's");
        qunsetenv("LSW_TEST_FRAME_HOST_CLAIM");

        mirrors.open_desktop(QStringLiteral("test desktop"));
        check(wait_for([&] { return desktops == desktops_before + 2; }) && mirrored().isEmpty(),
              "opening the desktop again closes the application and window mirrors");
        push_event({{"event", "launch_ended"}, {"pid", 321}, {"created", 777}});
        check(wait_for([&] { return !launcher.running(second); }),
              "an application that ends while the desktop shows it stops running");
        guest_windows = {window(123, 123, true, "C:\\first.exe")};
        close_desktop();
        check(wait_for([&] { return mirrored() == QList<quint32>{123, 666} && !launcher.running(third); }),
              "an application that shows no window when the desktop closes is no longer running");
        check(launcher.running(store),
              "a store application, whose windows another process may own, is mirrored again");
        check(launcher.running(first), "an application that still shows a window is mirrored again");
        launch_pid = 123;
    }
    QFile::remove(frame);
    const QString bad_executable = directory.filePath("not-executable");
    QFile bad_file(bad_executable);
    check(bad_file.open(QIODevice::WriteOnly), "non-executable fixture available");
    bad_file.close();
    for (const auto& mode :
         {QByteArray("mismatch"), QByteArray("early-exit"), QByteArray("invalid-port"),
          QByteArray("timeout"), QByteArray("failed-start")}) {
        qputenv("LSW_TEST_MIRROR_MODE", mode);
        qputenv("LSW_REMOTEWIN",
                (mode == "failed-start" ? bad_executable : QCoreApplication::applicationFilePath())
                    .toUtf8());
        std::unique_ptr<lsw::host::mirror::WindowMirror> mirror(
            lsw::host::mirror::WindowMirror::of_application(rpc, {123, {}, {}, {}, {}}));
        QString reason;
        int errors = 0;
        int finished = 0;
        QObject::connect(mirror.get(), &lsw::host::mirror::WindowMirror::failed, &app,
                         [&](const QString& error) {
                             reason = error;
                             ++errors;
                         });
        QObject::connect(mirror.get(), &lsw::host::mirror::WindowMirror::finished, &app,
                         [&] { ++finished; });
        check(mirror->start(), "failure scenario starts asynchronously");
        if (mode == "timeout")
            mirror->findChild<QTimer*>()->setInterval(30);
        check(wait_for([&] { return finished == 1; }), "failed process is reaped");
        check(errors == 1 && !reason.isEmpty(), "failure reported exactly once with a reason");
        if (mode == "mismatch")
            check(reason.contains("versions"), "version mismatch has an actionable error");
    }
    {
        qputenv("LSW_TEST_MIRROR_MODE", "early-exit");
        qputenv("LSW_REMOTEWIN", QCoreApplication::applicationFilePath().toUtf8());
        lsw::host::mirror::GuestMirrors mirrors(rpc);
        int opens = 0;
        int closes = 0;
        int errors = 0;
        QObject::connect(&mirrors, &lsw::host::mirror::GuestMirrors::desktop_opened, &app, [&] { ++opens; });
        QObject::connect(&mirrors, &lsw::host::mirror::GuestMirrors::desktop_closed, &app, [&] { ++closes; });
        QObject::connect(&mirrors, &lsw::host::mirror::GuestMirrors::desktop_failed, &app,
                         [&](const QString&) { ++errors; });
        mirrors.open_desktop(QStringLiteral("failing desktop"));
        check(wait_for([&] { return closes == 1; }) && opens == 1 && errors == 1 && !mirrors.desktop_open(),
              "a desktop mirror that fails is said to have opened and to have closed, once each");
    }
    QThreadPool::globalInstance()->waitForDone();
    return failures == 0 ? 0 : 1;
}
