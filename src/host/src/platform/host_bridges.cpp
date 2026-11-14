#include "lsw/host/platform/host_bridges.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>
#include <sys/xattr.h>

namespace lsw::host::platform {

namespace {

constexpr int kDeepestInclude = 8;
constexpr QLatin1StringView kAll{"all"};

bool privileged(const QString& path) {
    const QByteArray native = QFile::encodeName(path);
    struct stat info {};
    if (::stat(native.constData(), &info) != 0) {
        return false;
    }
    if ((info.st_mode & S_ISUID) != 0 && info.st_uid == 0) {
        return true;
    }
    return ::getxattr(native.constData(), "security.capability", nullptr, 0) > 0;
}

}

HostBridges::Places HostBridges::system_places() {
    Places places;
    places.interfaces = QStringLiteral("/sys/class/net");
    places.helpers = {
        QStringLiteral("/usr/lib/qemu/qemu-bridge-helper"),
        QStringLiteral("/usr/libexec/qemu-bridge-helper"),
        QStringLiteral("/usr/lib/qemu-bridge-helper"),
        QStringLiteral("/usr/local/libexec/qemu-bridge-helper"),
    };
    places.rules_file = QStringLiteral("/etc/qemu/bridge.conf");
    return places;
}

HostBridges::Facts HostBridges::probe(const Places& places) {
    Facts facts;
    const QDir interfaces(places.interfaces);
    for (const QString& name : interfaces.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (QFileInfo(interfaces.filePath(name + QStringLiteral("/bridge"))).isDir()) {
            facts.bridges << name;
        }
    }
    facts.helper_places = places.helpers;
    for (const QString& helper : places.helpers) {
        if (QFileInfo(helper).isExecutable()) {
            facts.helper = helper;
            facts.helper_privileged = privileged(helper);
            break;
        }
    }
    facts.rules_file = places.rules_file;
    facts.rules_readable = read_rules(places.rules_file, facts.rules, 0);
    return facts;
}

bool HostBridges::read_rules(const QString& path, QVector<Rule>& rules, int depth) {
    QFile file(path);
    if (depth > kDeepestInclude || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    bool complete = true;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QString verb = line.section(QLatin1Char(' '), 0, 0);
        const QString subject = line.section(QLatin1Char(' '), 1).trimmed();
        if (verb == QLatin1StringView("include")) {
            complete = read_rules(subject, rules, depth + 1) && complete;
        } else if (verb == QLatin1StringView("allow") || verb == QLatin1StringView("deny")) {
            rules.push_back({verb == QLatin1StringView("allow"), subject == kAll ? QString{} : subject});
        }
    }
    return complete;
}

QString HostBridges::missing(const Facts& facts, const QString& bridge) {
    if (bridge.isEmpty()) {
        return QStringLiteral("no bridge is chosen.");
    }
    if (!facts.bridges.contains(bridge)) {
        return QStringLiteral("this computer has no bridge named %1.").arg(bridge);
    }
    if (facts.helper.isEmpty()) {
        return QStringLiteral("QEMU's bridge helper is not installed (looked for %1).")
            .arg(facts.helper_places.join(QStringLiteral(", ")));
    }
    if (!facts.helper_privileged) {
        return QStringLiteral("%1 is neither setuid root nor given a capability, so it cannot "
                              "attach the guest to a bridge.")
            .arg(facts.helper);
    }
    bool allowed = false;
    bool denied = false;
    for (const Rule& rule : facts.rules) {
        if (rule.bridge.isEmpty() || rule.bridge == bridge) {
            (rule.allow ? allowed : denied) = true;
        }
    }
    if (denied) {
        return QStringLiteral("%1 denies %2.").arg(facts.rules_file, bridge);
    }
    if (!allowed) {
        return facts.rules_readable
                   ? QStringLiteral("%1 does not allow %2; it needs the line \"allow %2\".")
                         .arg(facts.rules_file, bridge)
                   : QStringLiteral("%1 cannot be read; it needs the line \"allow %2\".")
                         .arg(facts.rules_file, bridge);
    }
    return {};
}

}
