#include "lsw/host/manager/desktop_preview.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/linkproto/envelope.hpp"

#include "host/mirror/monitor_window.hpp"
#include "host/mirror/gpu_picture.hpp"
#include "proto/message_writer.hpp"
#include "proto/wire_hello.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThreadPool>
#include <QTimer>
#include <QWheelEvent>
#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <functional>
#include <memory>

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) ++failures;
}
bool wait_for(const std::function<bool()>& done, int milliseconds = 3000) {
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    poll.start(5);
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    if (!done()) loop.exec();
    return done();
}
// A click on the picture, where the play and pause badge is.
void click(QWidget& preview) {
    QWidget* surface = preview.childAt(preview.rect().center());
    if (surface == nullptr) return;
    const QPointF at(surface->rect().center());
    QMouseEvent press(QEvent::MouseButtonPress, at, surface->mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, at, surface->mapToGlobal(at), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &press);
    QApplication::sendEvent(surface, &release);
}
void send(QTcpSocket& socket, rwin::MessageWriter message) {
    const auto bytes = message.take();
    socket.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    lsw::host::link::AgentRpcClient rpc;
    if (app.arguments().contains(QStringLiteral("--live"))) {
        rpc.set_target(1, 5102);
        lsw::host::manager::DesktopPreview preview(rpc);
        preview.resize(640, 360);
        preview.set_paused(false);
        preview.show();
        preview.set_available(true);
        int swaps = 0;
        if (auto* picture = preview.findChild<rwin::host::GpuPicture*>()) {
            QObject::connect(picture, &QOpenGLWidget::frameSwapped, &app, [&] { ++swaps; });
        }
        check(wait_for([&] { return preview.status() == QStringLiteral("WGC, GPU") && swaps > 0; }, 30000),
              "real guest monitor reaches embedded GPU preview");
        if (failures != 0) std::fprintf(stderr, "%s\n", qPrintable(preview.status()));
        if (app.arguments().contains(QStringLiteral("--snapshot"))) {
            // Diagnostic only: grabbing the host widget reads its framebuffer.
            check(preview.grab().save(QStringLiteral("/tmp/opencode/wgc-preview.png")),
                  "save diagnostic preview snapshot");
        }
        click(preview);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(preview.is_paused() && preview.status() == QStringLiteral("WGC, paused") &&
                  preview.findChildren<rwin::host::MonitorWindow*>().isEmpty(),
              "live Pause releases the GPU capture session");
        click(preview);
        swaps = 0;
        if (auto* picture = preview.findChild<rwin::host::GpuPicture*>()) {
            QObject::connect(picture, &QOpenGLWidget::frameSwapped, &app, [&] { ++swaps; });
        }
        check(wait_for([&] { return preview.status() == QStringLiteral("WGC, GPU") && swaps > 0; }, 30000),
              "live Play resumes GPU rendering without readback");
        preview.hide();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThreadPool::globalInstance()->waitForDone();
        return failures == 0 ? 0 : 1;
    }

    // No real export socket, guest or VM: exercise the WGC pixel fallback
    // and lifecycle with a fake agent and a real RemoteWin connection.
    qputenv("XDG_RUNTIME_DIR", "/tmp/opencode/preview-test-no-export");
    QTcpServer agent;
    check(agent.listen(QHostAddress::LocalHost, 0), "fake agent listening");
    int starts = 0;
    bool refuse = false;
    QTcpSocket guest;
    QObject::connect(&agent, &QTcpServer::newConnection, &app, [&] {
        auto* socket = agent.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, &app, [&, socket, buffer] {
            *buffer += socket->readAll();
            while (buffer->size() >= 5) {
                const auto size = lsw::linkproto::read_payload_len(
                    reinterpret_cast<const std::uint8_t*>(buffer->constData()));
                if (buffer->size() < 5 + size) break;
                const auto request = nlohmann::json::parse(buffer->mid(5, size).toStdString());
                buffer->remove(0, 5 + size);
                ++starts;
                check(request["cmd"] == "start_window_mirror" &&
                          request["params"].value("desktop", false), "preview requests monitor capture");
                const auto port = request["params"]["port"].get<quint16>();
                check(port != 0, "guest receives the actual ephemeral listening port");
                nlohmann::json result = refuse
                    ? nlohmann::json{{"ok", false}, {"error", "test refusal"}}
                    : nlohmann::json{{"ok", true}, {"pid", 456}};
                const auto reply = nlohmann::json{{"id", request["id"]}, {"result", result}}.dump();
                std::uint8_t header[5];
                lsw::linkproto::write_header(header, lsw::linkproto::FrameType::Control,
                                             static_cast<std::uint32_t>(reply.size()));
                socket->write(reinterpret_cast<const char*>(header), 5);
                socket->write(reply.data(), static_cast<qint64>(reply.size()));
                if (!refuse) guest.connectToHost(QHostAddress::LocalHost, port);
            }
        });
    });
    QObject::connect(&guest, &QTcpSocket::connected, &app, [&] {
        rwin::MessageWriter hello(rwin::MessageId::GuestHello);
        rwin::WireHello{}.write(hello);
        send(guest, std::move(hello));
        rwin::MessageWriter frame(rwin::MessageId::Frame);
        frame.put_u64(rwin::kMonitorWindow);
        frame.put_u32(2);
        frame.put_u32(1);
        frame.put_u32(0xffffffff);
        frame.put_u32(0xff000000);
        send(guest, std::move(frame));
    });
    rpc.set_target(1, agent.serverPort());
    {
        lsw::host::manager::DesktopPreview preview(rpc);
        preview.resize(320, 220);
        preview.set_available(true);
        check(starts == 0, "hidden preview starts no capture");
        preview.show();
        check(wait_for([&] { return starts == 1 && guest.state() == QAbstractSocket::UnconnectedState; }) &&
                  preview.is_paused() && preview.status() == QStringLiteral("WGC, paused"),
              "a paused start takes one frame and lets the capture go");
        click(preview);
        check(wait_for([&] { return starts == 2 && preview.status() == QStringLiteral("WGC, pixels"); }),
              "a click plays the WGC capture live");
        preview.set_available(true);
        check(starts == 2, "repeated agent status does not duplicate captures");
        const auto views = preview.findChildren<rwin::host::MonitorWindow*>();
        check(views.size() == 1 && !views.first()->isWindow() &&
                  views.first()->size().width() >= 300,
              "monitor is embedded and first frame cannot resize its container");
        if (views.size() == 1) {
            auto* view = views.first();
            check(view->focusPolicy() == Qt::NoFocus &&
                      view->testAttribute(Qt::WA_TransparentForMouseEvents),
                  "preview cannot take keyboard focus or pointer input");
            // Drain the handshake, FrameRoute and WatchMonitor before input.
            QCoreApplication::processEvents();
            guest.readAll();
            const QPointF at(view->rect().center());
            QMouseEvent move(QEvent::MouseMove, at, at,
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QMouseEvent press(QEvent::MouseButtonPress, at, at,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, at, at,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QWheelEvent wheel(at, at, QPoint(), QPoint(0, 120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QKeyEvent down(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
            QKeyEvent up(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
            const std::array<QEvent*, 6> events{&move, &press, &release, &wheel, &down, &up};
            for (QEvent* event : events)
                QApplication::sendEvent(view, event);
            check(!wait_for([&] { return guest.bytesAvailable() != 0; }, 100),
                  "preview forwards no move, button, wheel or key messages");
            view->set_interactive(true);
            for (QEvent* event : events) QApplication::sendEvent(view, event);
            check(wait_for([&] { return guest.bytesAvailable() != 0; }),
                  "ordinary desktop views still transmit input");
            view->set_interactive(false);
        }
        preview.hide();
        check(wait_for([&] { return guest.state() == QAbstractSocket::UnconnectedState; }),
              "hiding preview detaches guest capture");
        preview.show();
        check(wait_for([&] { return starts == 3 && preview.status() == QStringLiteral("WGC, pixels"); }),
              "showing preview reconnects with a fresh session");
        click(preview);
        check(wait_for([&] { return guest.state() == QAbstractSocket::UnconnectedState; }) &&
                  preview.is_paused() && preview.status() == QStringLiteral("WGC, paused"),
              "Pause stops capture instead of merely suppressing repaint");
        preview.set_available(true);
        check(starts == 3 && preview.is_paused(), "agent polling cannot override manual pause");
        click(preview);
        check(wait_for([&] { return starts == 4 && preview.status() == QStringLiteral("WGC, pixels"); }),
              "Play resumes WGC capture with a fresh connection");
        guest.disconnectFromHost();
        check(wait_for([&] { return preview.status().contains(QStringLiteral("disconnected")); }),
              "guest disconnect gives an actionable preview failure");
        refuse = true;
        click(preview);
        check(wait_for([&] { return preview.status().contains(QStringLiteral("test refusal")); }),
              "retry reports the agent's startup refusal");
        refuse = false;
        click(preview);
        check(wait_for([&] { return starts == 6 && preview.status() == QStringLiteral("WGC, pixels"); }),
              "retry recovers without a VM restart");
        preview.clear(QStringLiteral("WGC, the guest is off"));
        check(wait_for([&] { return guest.state() == QAbstractSocket::UnconnectedState; }),
              "target change releases old monitor capture");
        check(preview.status() == QStringLiteral("WGC, the guest is off"),
              "cleared preview cannot show the previous VM as connected");
    }
    QThreadPool::globalInstance()->waitForDone();
    return failures == 0 ? 0 : 1;
}
