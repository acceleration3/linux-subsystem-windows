#include "lsw/host/platform/indexer_exclusions.hpp"

#include "lsw/base/log.hpp"

#include <QRegularExpression>

namespace lsw::host::platform {

namespace {

const QString kBalooFile = QStringLiteral("baloofilerc");
const QString kBalooGroup = QStringLiteral("General");
const QString kBalooKey = QStringLiteral("exclude folders");
const QString kTrackerSchema = QStringLiteral("org.freedesktop.Tracker3.Miner.Files");
const QString kTrackerKey = QStringLiteral("ignored-directories");

QString expand(QString entry, const QString& home) {
    if (entry == QLatin1String("$HOME") || entry.startsWith(QLatin1String("$HOME/"))) {
        entry = home + entry.mid(5);
    }
    return entry;
}

QString canonical(const QString& entry, const QString& home) {
    QString out = expand(entry, home);
    while (out.size() > 1 && out.endsWith(QLatin1Char('/'))) {
        out.chop(1);
    }
    return out;
}

}  // namespace

IndexerExclusions::IndexerExclusions(CommandRunner& runner, QString home)
    : runner_(runner), home_(std::move(home)) {}

QStringList IndexerExclusions::parse_baloo(const QString& value) {
    QStringList out;
    for (const QString& part : value.trimmed().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        out.push_back(part.trimmed());
    }
    return out;
}

QStringList IndexerExclusions::parse_tracker(const QString& value) {
    static const QRegularExpression item{QStringLiteral("'((?:[^'\\\\]|\\\\.)*)'")};
    QStringList out;
    auto it = item.globalMatch(value);
    while (it.hasNext()) {
        QString text = it.next().captured(1);
        text.replace(QRegularExpression{QStringLiteral("\\\\(.)")}, QStringLiteral("\\1"));
        out.push_back(text);
    }
    return out;
}

QString IndexerExclusions::format_tracker(const QStringList& values) {
    QStringList quoted;
    for (QString value : values) {
        value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        value.replace(QLatin1Char('\''), QStringLiteral("\\'"));
        quoted.push_back(QLatin1Char('\'') + value + QLatin1Char('\''));
    }
    return QLatin1Char('[') + quoted.join(QStringLiteral(", ")) + QLatin1Char(']');
}

QStringList IndexerExclusions::merge(const QStringList& existing, const QString& path,
                                     const QString& home, bool& changed) {
    const QString wanted = canonical(path, home);
    QStringList out = existing;
    changed = false;
    for (const QString& entry : existing) {
        if (canonical(entry, home) == wanted) {
            return out;
        }
    }
    out.push_back(wanted);
    changed = true;
    return out;
}

void IndexerExclusions::apply(const QString& path) {
    apply_baloo(path);
    apply_tracker(path);
}

void IndexerExclusions::apply_baloo(const QString& path) {
    const QString reader = QStringLiteral("kreadconfig6");
    const QString writer = QStringLiteral("kwriteconfig6");
    if (!runner_.available(reader) || !runner_.available(writer)) {
        return;
    }
    const auto current =
        runner_.run(reader, {QStringLiteral("--file"), kBalooFile, QStringLiteral("--group"),
                             kBalooGroup, QStringLiteral("--key"), kBalooKey});
    if (!current.ok) {
        return;
    }
    QStringList existing;
    for (const QString& entry : parse_baloo(current.text)) {
        existing.push_back(expand(entry, home_));
    }
    bool changed = false;
    const QStringList merged = merge(existing, path, home_, changed);
    if (!changed) {
        return;
    }
    const auto written = runner_.run(
        writer, {QStringLiteral("--file"), kBalooFile, QStringLiteral("--group"), kBalooGroup,
                 QStringLiteral("--key"), kBalooKey, merged.join(QLatin1Char(','))});
    if (written.ok) {
        LSW_INFO("excluded {} from Baloo", path.toStdString());
    }
}

void IndexerExclusions::apply_tracker(const QString& path) {
    const QString tool = QStringLiteral("gsettings");
    if (!runner_.available(tool)) {
        return;
    }
    const auto current = runner_.run(tool, {QStringLiteral("get"), kTrackerSchema, kTrackerKey});
    if (!current.ok) {
        return;
    }
    bool changed = false;
    const QStringList merged = merge(parse_tracker(current.text), path, home_, changed);
    if (!changed) {
        return;
    }
    const auto written = runner_.run(
        tool, {QStringLiteral("set"), kTrackerSchema, kTrackerKey, format_tracker(merged)});
    if (written.ok) {
        LSW_INFO("excluded {} from Tracker", path.toStdString());
    }
}

}  // namespace lsw::host::platform
