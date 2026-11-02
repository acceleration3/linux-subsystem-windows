#include "lsw/host/services/settings_report.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace lsw::host::services {

namespace {

constexpr const char* kRemoved = "(removed)";

QString shown(const QVariant& value) {
    if (value.typeId() == QMetaType::QStringList) {
        return value.toStringList().join(QStringLiteral(", "));
    }
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    const QString text = value.toString();
    return text.isEmpty() ? QStringLiteral("-") : text;
}

}

bool SettingsReport::secret(const QString& key) {
    static const QRegularExpression words{
        QStringLiteral("(^|[^a-z0-9])(pass|passwd|password|passphrase|secret|secrets|token|"
                       "tokens|key|keys|credential|credentials|licen[cs]e)([^a-z0-9]|$)"),
        QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression joined{
        QStringLiteral("(password|passwd|secret|token|productkey|apikey|credential)"),
        QRegularExpression::CaseInsensitiveOption};
    return words.match(key).hasMatch() || joined.match(key).hasMatch();
}

QString SettingsReport::render(const config::ManagerSettings& manager,
                               const config::Settings& settings) {
    QString out = QStringLiteral("== Manager settings (%1) ==\n").arg(manager.file_path());
    const auto values = manager.snapshot();
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        out += QStringLiteral("%1 = %2\n")
                   .arg(it.key(), secret(it.key()) ? QString::fromLatin1(kRemoved)
                                                   : shown(it.value()));
    }
    out += QStringLiteral("\n== Folders ==\n");
    out += QStringLiteral("data = %1\n").arg(settings.data_dir());
    out += QStringLiteral("config = %1\n").arg(settings.config_dir());
    out += QStringLiteral("cache = %1\n").arg(settings.cache_dir());
    out += QStringLiteral("logs = %1\n").arg(settings.logs_dir());
    out += QStringLiteral("disks = %1\n").arg(settings.disks_dir());
    out += QStringLiteral("first run = %1\n").arg(settings.is_first_run() ? QStringLiteral("yes") : QStringLiteral("no"));
    return out;
}

}
