#include "lsw/host/services/driver_manifest.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

namespace lsw::host::services {

DriverManifest::DriverManifest(const QString& text) {
    // sha256sum's "<hash>  <name>", or "<hash> *<name>" in binary mode.
    static const QRegularExpression line(QStringLiteral("^([0-9A-Fa-f]{64}) [ *](.+)$"));
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = line.match(raw.trimmed());
        if (m.hasMatch()) {
            files_.insert(m.captured(2).trimmed().toLower(), m.captured(1).toLower());
        }
    }
}

DriverManifest DriverManifest::read(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return DriverManifest{QString::fromUtf8(file.readAll())};
}

void DriverManifest::add(const QString& name, const QString& path) {
    QFile file{path};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (file.open(QIODevice::ReadOnly) && hash.addData(&file)) {
        files_.insert(name.toLower(), QString::fromLatin1(hash.result().toHex()));
    }
}

QStringList DriverManifest::differences(const DriverManifest& other) const {
    QSet<QString> names;
    for (auto it = files_.cbegin(); it != files_.cend(); ++it) {
        names.insert(it.key());
    }
    for (auto it = other.files_.cbegin(); it != other.files_.cend(); ++it) {
        names.insert(it.key());
    }
    QStringList out;
    for (const QString& name : names) {
        if (files_.value(name) != other.files_.value(name)) {
            out.push_back(name);
        }
    }
    out.sort();
    return out;
}

}  // namespace lsw::host::services
