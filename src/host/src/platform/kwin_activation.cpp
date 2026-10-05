#include "lsw/host/platform/kwin_activation.hpp"

#include "lsw/base/log.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QTemporaryFile>
#include <QTimer>

namespace lsw::host::platform {

namespace {

constexpr const char* kService = "org.kde.KWin";
constexpr const char* kScripting = "/Scripting";
constexpr const char* kScriptingInterface = "org.kde.kwin.Scripting";
constexpr const char* kScriptInterface = "org.kde.kwin.Script";

// KWin reads the file as the script runs, and the script runs a moment
// after ``run`` returns; unloaded and deleted only once that is surely
// over.
constexpr int kUnloadAfterMs = 5000;

// KWin 6's scripting API (workspace.windowList, workspace.activeWindow).
constexpr const char* kScript = R"JS(
workspace.windowList().forEach(function(w) {
    if (w.pid !== %1 || !w.normalWindow) return;
    if (w.minimized) w.minimized = false;
    workspace.activeWindow = w;
});
)JS";

QDBusMessage call(const QString& path, const char* interface, const char* method,
                  const QVariantList& arguments) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(kService), path, QLatin1String(interface), QLatin1String(method));
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().call(message);
}

void unload(const QString& name) {
    const QDBusMessage answer =
        call(QLatin1String(kScripting), kScriptingInterface, "unloadScript", {name});
    if (answer.type() == QDBusMessage::ErrorMessage) {
        LSW_WARN("kwin: script {} could not be unloaded: {}", name.toStdString(),
                 answer.errorMessage().toStdString());
    }
}

}  // namespace

bool KWinActivation::activate(qint64 pid) {
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || bus.interface() == nullptr
        || !bus.interface()->isServiceRegistered(QLatin1String(kService)).value()) {
        return false;
    }
    auto* file = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/lsw-raise-XXXXXX.js"));
    if (!file->open() || file->write(QString::fromLatin1(kScript).arg(pid).toUtf8()) < 0
        || !file->flush()) {
        LSW_WARN("kwin: cannot write the raise script: {}", file->errorString().toStdString());
        delete file;
        return false;
    }
    const QString name = QStringLiteral("lsw-raise-%1").arg(pid);
    // A script of the same name still loaded from a raise a moment ago
    // would make KWin refuse this one.
    unload(name);
    const QDBusMessage loaded = call(QLatin1String(kScripting), kScriptingInterface,
                                     "loadScript", {file->fileName(), name});
    const int id = loaded.arguments().isEmpty() ? -1 : loaded.arguments().constFirst().toInt();
    if (loaded.type() == QDBusMessage::ErrorMessage || id < 0) {
        LSW_WARN("kwin: loadScript refused the raise script: {}",
                 loaded.errorMessage().toStdString());
        delete file;
        return false;
    }
    const QDBusMessage ran =
        call(QStringLiteral("/Scripting/Script%1").arg(id), kScriptInterface, "run", {});
    QTimer::singleShot(kUnloadAfterMs, [name, file] {
        unload(name);
        delete file;
    });
    if (ran.type() == QDBusMessage::ErrorMessage) {
        LSW_WARN("kwin: the raise script would not run: {}", ran.errorMessage().toStdString());
        return false;
    }
    return true;
}

}  // namespace lsw::host::platform
