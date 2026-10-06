// Add a store app: what the agent lists is shown with its icons, the
// search narrows it by name or package family, and Add makes an entry
// named by the application's ID. A fake agent answers list_store_apps.
// ``--snapshot <dir>`` saves the dialog and the app grid as drawn.
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/app_list.hpp"
#include "lsw/host/manager/store_app_dialog.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/linkproto/envelope.hpp"

#include <QApplication>
#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <nlohmann/json.hpp>

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
std::string icon(QColor colour) {
    QImage image(56, 56, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(colour);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(2, 2, 52, 52), 12, 12);
    p.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png.toBase64().toStdString();
}
template <class T>
T* only(QObject& root) {
    const auto found = root.findChildren<T*>();
    return found.size() == 1 ? found.front() : nullptr;
}
int shown(const QListWidget& list) {
    int rows = 0;
    for (int row = 0; row < list.count(); ++row) rows += list.item(row)->isHidden() ? 0 : 1;
    return rows;
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    lsw::host::ui::Theme::apply();
    const int snapshot_at = app.arguments().indexOf(QStringLiteral("--snapshot"));
    const QString snapshots = snapshot_at > 0 ? app.arguments().value(snapshot_at + 1) : QString{};

    bool refuse = false;
    QTcpServer agent;
    check(agent.listen(QHostAddress::LocalHost, 0), "fake agent listening");
    const nlohmann::json listed = {
        {"apps",
         {{{"aumid", "Canva.Affinity_8a0j1tnjnt4a4!Canva.Affinity"}, {"name", "Affinity"},
           {"family", "Canva.Affinity_8a0j1tnjnt4a4"}, {"icon_png_base64", icon(QColor("#a6e86b"))}},
          {{"aumid", "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"}, {"name", "Calculator"},
           {"family", "Microsoft.WindowsCalculator_8wekyb3d8bbwe"},
           {"icon_png_base64", icon(QColor("#5aa9f0"))}},
          {{"aumid", "Microsoft.WindowsNotepad_8wekyb3d8bbwe!App"}, {"name", "Notepad"},
           {"family", "Microsoft.WindowsNotepad_8wekyb3d8bbwe"},
           {"icon_png_base64", icon(QColor("#8b93a4"))}}}}};
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
                nlohmann::json body;
                if (request["cmd"] == "list_store_apps" && !refuse) {
                    body = {{"id", request["id"]}, {"result", listed}};
                } else {
                    body = {{"id", request["id"]}, {"error", "the session helper in session 1 is not running"}};
                }
                const auto reply = body.dump();
                std::uint8_t header[5];
                lsw::linkproto::write_header(header, lsw::linkproto::FrameType::Control,
                                             static_cast<std::uint32_t>(reply.size()));
                socket->write(reinterpret_cast<const char*>(header), 5);
                socket->write(reply.data(), static_cast<qint64>(reply.size()));
            }
        });
    });
    lsw::host::link::AgentRpcClient rpc;
    rpc.set_target(1, agent.serverPort());

    {
        lsw::host::manager::StoreAppDialog dialog(rpc);
        dialog.show();
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("StoreApps"));
        auto* search = only<QLineEdit>(dialog);
        check(list != nullptr && search != nullptr, "the dialog has its search and its list");
        if (list == nullptr || search == nullptr) return 1;
        check(wait_for([&] { return list->count() == 3; }), "every store app the agent lists is a row");
        check(!list->item(0)->icon().isNull(), "rows carry the icons the agent sent");
        check(list->currentRow() == 0, "the first row is selected, ready for Enter");
        search->setText(QStringLiteral("calc"));
        check(shown(*list) == 1 && !list->item(1)->isHidden(), "the search narrows by name");
        search->setText(QStringLiteral("8a0j1"));
        check(shown(*list) == 1 && !list->item(0)->isHidden(), "and by package family");
        search->setText(QStringLiteral("AFF"));
        if (!snapshots.isEmpty()) {
            search->setText(QString{});
            QCoreApplication::processEvents();
            dialog.grab().save(snapshots + QStringLiteral("/store-dialog.png"));
            search->setText(QStringLiteral("affin"));
            QCoreApplication::processEvents();
            dialog.grab().save(snapshots + QStringLiteral("/store-dialog-search.png"));
        }
        search->setText(QStringLiteral("zzz"));
        check(shown(*list) == 0 && !dialog.findChildren<QLabel*>().isEmpty(), "nothing matches: the empty word");
        if (!snapshots.isEmpty()) dialog.grab().save(snapshots + QStringLiteral("/store-dialog-none.png"));
        search->setText(QStringLiteral("affinity"));
        auto* add = dialog.findChildren<QPushButton*>().back();
        check(add->text() == QStringLiteral("Add") && add->isEnabled(), "Add is ready with a row picked");
        add->click();
        const auto& chosen = dialog.chosen();
        check(dialog.result() == QDialog::Accepted &&
                  chosen.aumid == QStringLiteral("Canva.Affinity_8a0j1tnjnt4a4!Canva.Affinity") &&
                  chosen.path.isEmpty() && chosen.name == QStringLiteral("Affinity") &&
                  !chosen.id.isEmpty() && !chosen.icon_image().isNull(),
              "Add makes an entry named by the application's ID, with its icon and no path");
        check(chosen.target() == chosen.aumid, "a store entry's launch target is its ID");
    }
    {
        refuse = true;
        lsw::host::manager::StoreAppDialog dialog(rpc);
        dialog.show();
        check(wait_for([&] {
                  for (auto* label : dialog.findChildren<QLabel*>()) {
                      if (label->isVisible() && label->text().contains(QStringLiteral("not running"))) return true;
                  }
                  return false;
              }),
              "an agent that cannot list says why in place of the list");
        if (!snapshots.isEmpty()) dialog.grab().save(snapshots + QStringLiteral("/store-dialog-offline.png"));
        refuse = false;
    }
    if (!snapshots.isEmpty()) {
        QTemporaryDir dir;
        lsw::host::apps::AppConfigStore store(dir.path());
        for (const auto& entry : listed["apps"]) {
            lsw::host::apps::AppConfig cfg;
            cfg.aumid = QString::fromStdString(entry["aumid"].get<std::string>());
            cfg.name = QString::fromStdString(entry["name"].get<std::string>());
            cfg.icon_png_base64 = QString::fromStdString(entry["icon_png_base64"].get<std::string>());
            store.upsert(cfg);
        }
        lsw::host::apps::GuestAppLauncher launcher(rpc);
        lsw::host::manager::AppListWidget grid(store, rpc, launcher);
        grid.resize(420, 240);
        grid.show();
        // The fake agent refuses the activation; until it answers the tile
        // is opening.
        launcher.launch(store.configs().front());
        QCoreApplication::processEvents();
        grid.grab().save(snapshots + QStringLiteral("/grid-opening.png"));
        wait_for([] { return false; }, 300);
    }
    QThreadPool::globalInstance()->waitForDone();
    return failures == 0 ? 0 : 1;
}
