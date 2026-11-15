#include "lsw/host/platform/cpu_scope.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QFile>
#include <QList>
#include <utility>

namespace lsw::host::platform {

namespace {

constexpr auto kService = "org.freedesktop.systemd1";
constexpr auto kPath = "/org/freedesktop/systemd1";
constexpr auto kInterface = "org.freedesktop.systemd1.Manager";
constexpr int kCallTimeoutMs = 3000;
constexpr qint64 kMaxCgroup = 64 * 1024;

struct UnitProperty {
    QString name;
    QDBusVariant value;
};

struct UnitAuxiliary {
    QString name;
    QList<UnitProperty> properties;
};

QDBusArgument& operator<<(QDBusArgument& argument, const UnitProperty& property) {
    argument.beginStructure();
    argument << property.name << property.value;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, UnitProperty& property) {
    argument.beginStructure();
    argument >> property.name >> property.value;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const UnitAuxiliary& auxiliary) {
    argument.beginStructure();
    argument << auxiliary.name << auxiliary.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, UnitAuxiliary& auxiliary) {
    argument.beginStructure();
    argument >> auxiliary.name >> auxiliary.properties;
    argument.endStructure();
    return argument;
}

void register_types() {
    static const bool once = [] {
        qDBusRegisterMetaType<UnitProperty>();
        qDBusRegisterMetaType<QList<UnitProperty>>();
        qDBusRegisterMetaType<UnitAuxiliary>();
        qDBusRegisterMetaType<QList<UnitAuxiliary>>();
        return true;
    }();
    Q_UNUSED(once);
}

QByteArray slurp(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(kMaxCgroup) : QByteArray();
}

UnitProperty cpu_weight(int weight) {
    return {QStringLiteral("CPUWeight"),
            QDBusVariant(QVariant::fromValue(static_cast<qulonglong>(weight)))};
}

}  // namespace

SystemdCpuScope::SystemdCpuScope(QString prefix) : prefix_(std::move(prefix)) {}

QString SystemdCpuScope::unit_name(qint64 pid) const {
    return QStringLiteral("%1-%2.scope").arg(prefix_).arg(pid);
}

void SystemdCpuScope::call(const QString& method, const QVariantList& arguments, Done done) {
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        done(false, QStringLiteral("no session bus"));
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    message.setArguments(arguments);
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, kCallTimeoutMs), &pending_);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, &pending_,
                     [done = std::move(done)](QDBusPendingCallWatcher* finished) {
                         const QDBusError error = finished->error();
                         const bool failed = finished->isError();
                         finished->deleteLater();
                         done(!failed, error.message().isEmpty() ? error.name() : error.message());
                     });
}

QString SystemdCpuScope::group_of(const QByteArray& cgroup) {
    for (const QByteArray& line : cgroup.split('\n')) {
        if (line.startsWith("0::/")) {
            return QString::fromUtf8(line.mid(3));
        }
    }
    return {};
}

bool SystemdCpuScope::holds(qint64 pid) {
    return group_of(slurp(QStringLiteral("/proc/%1/cgroup").arg(pid)))
        .endsWith(QLatin1Char('/') + unit_name(pid));
}

std::optional<int> SystemdCpuScope::weight(qint64 pid) {
    const QString group = group_of(slurp(QStringLiteral("/proc/%1/cgroup").arg(pid)));
    if (group.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const int weight =
        slurp(QStringLiteral("/sys/fs/cgroup%1/cpu.weight").arg(group)).trimmed().toInt(&ok);
    return ok ? std::optional<int>{weight} : std::nullopt;
}

void SystemdCpuScope::enter(qint64 pid, int weight, Done done) {
    register_types();
    const QList<UnitProperty> properties = {
        {QStringLiteral("Description"), QDBusVariant(QStringLiteral("LSW Windows guest process"))},
        {QStringLiteral("CollectMode"), QDBusVariant(QStringLiteral("inactive-or-failed"))},
        {QStringLiteral("PIDs"),
         QDBusVariant(QVariant::fromValue(QList<uint>{static_cast<uint>(pid)}))},
        cpu_weight(weight),
    };
    call(QStringLiteral("StartTransientUnit"),
         {unit_name(pid), QStringLiteral("fail"), QVariant::fromValue(properties),
          QVariant::fromValue(QList<UnitAuxiliary>{})},
         std::move(done));
}

void SystemdCpuScope::set_weight(qint64 pid, int weight, Done done) {
    register_types();
    call(QStringLiteral("SetUnitProperties"),
         {unit_name(pid), true, QVariant::fromValue(QList<UnitProperty>{cpu_weight(weight)})},
         std::move(done));
}

}  // namespace lsw::host::platform
