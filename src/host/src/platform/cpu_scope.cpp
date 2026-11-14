#include "lsw/host/platform/cpu_scope.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QFile>
#include <QList>

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
    return {QStringLiteral("CPUWeight"), QDBusVariant(QVariant::fromValue(static_cast<qulonglong>(weight)))};
}

bool call(const QString& method, const QVariantList& arguments, QString* error_out) {
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("no session bus");
        }
        return false;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    message.setArguments(arguments);
    const QDBusMessage reply = bus.call(message, QDBus::Block, kCallTimeoutMs);
    if (reply.type() == QDBusMessage::ReplyMessage) {
        return true;
    }
    if (error_out != nullptr) {
        *error_out = reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
    }
    return false;
}

}

QString SystemdCpuScope::unit_name(qint64 pid) {
    return QStringLiteral("lsw-vm-%1.scope").arg(pid);
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

bool SystemdCpuScope::enter(qint64 pid, int weight, QString* error_out) {
    register_types();
    const QList<UnitProperty> properties = {
        {QStringLiteral("Description"), QDBusVariant(QStringLiteral("LSW Windows guest"))},
        {QStringLiteral("PIDs"), QDBusVariant(QVariant::fromValue(QList<uint>{static_cast<uint>(pid)}))},
        cpu_weight(weight),
    };
    return call(QStringLiteral("StartTransientUnit"),
                {unit_name(pid), QStringLiteral("fail"), QVariant::fromValue(properties),
                 QVariant::fromValue(QList<UnitAuxiliary>{})},
                error_out);
}

bool SystemdCpuScope::set_weight(qint64 pid, int weight, QString* error_out) {
    register_types();
    return call(QStringLiteral("SetUnitProperties"),
                {unit_name(pid), true, QVariant::fromValue(QList<UnitProperty>{cpu_weight(weight)})},
                error_out);
}

}
