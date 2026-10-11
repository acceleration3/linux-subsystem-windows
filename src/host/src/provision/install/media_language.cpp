#include "lsw/host/provision/install/media_language.hpp"

#include "lsw/host/provision/install/seven_zip.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace lsw::host::provision::install {

namespace {

// Reading one small file from the medium; 7-Zip seeks to it.
constexpr int kExtractTimeoutMs = 60'000;

// A language tag in its usual case: "en-us" is "en-US", "sr-latn-rs"
// "sr-Latn-RS". Microsoft's media write them in lower case; Setup does
// not mind either way, people reading them do.
QString cased(const QString& tag) {
    QStringList parts = tag.split(QLatin1Char('-'));
    for (qsizetype i = 0; i < parts.size(); ++i) {
        QString& part = parts[i];
        if (i == 0) {
            part = part.toLower();
        } else if (part.size() == 4) {
            part = part.left(1).toUpper() + part.mid(1).toLower();
        } else if (part.size() == 2) {
            part = part.toUpper();
        }
    }
    return parts.join(QLatin1Char('-'));
}

}  // namespace

QStringList MediaLanguage::parse(const QString& lang_ini) {
    static const QRegularExpression language(QStringLiteral("^([A-Za-z]{2,3}(-[A-Za-z0-9]+)*)\\s*="));
    QStringList out;
    bool in_section = false;
    for (const QString& raw : lang_ini.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            in_section =
                line.compare(QStringLiteral("[Available UI Languages]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!in_section) {
            continue;
        }
        const QRegularExpressionMatch m = language.match(line);
        if (m.hasMatch() && !out.contains(m.captured(1), Qt::CaseInsensitive)) {
            out.push_back(cased(m.captured(1)));
        }
    }
    return out;
}

QStringList MediaLanguage::of(const QString& iso_path, const QString& scratch_dir,
                              QString* error_out) {
    const QStringList entries = seven_zip_entries(iso_path, error_out);
    QString entry;
    for (const QString& name : entries) {
        if (name.compare(QStringLiteral("sources/lang.ini"), Qt::CaseInsensitive) == 0) {
            entry = name;
            break;
        }
    }
    if (entry.isEmpty()) {
        if (error_out != nullptr && error_out->isEmpty()) {
            *error_out = QStringLiteral("%1 has no sources/lang.ini").arg(iso_path);
        }
        return {};
    }
    QTemporaryDir dir{QDir(scratch_dir).filePath(QStringLiteral("lang-XXXXXX"))};
    if (!dir.isValid()) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("could not make a scratch directory in %1").arg(scratch_dir);
        }
        return {};
    }
    const QStringList args{QStringLiteral("e"),     QStringLiteral("-y"),
                           QStringLiteral("-bso0"), QStringLiteral("-bsp0"),
                           iso_path,                entry,
                           QStringLiteral("-o%1").arg(dir.path())};
    if (!run_seven_zip(args, kExtractTimeoutMs, error_out)) {
        return {};
    }
    QFile file{QDir(dir.path()).filePath(entry.section(QLatin1Char('/'), -1))};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("sources/lang.ini was not extracted from %1").arg(iso_path);
        }
        return {};
    }
    // lang.ini is UTF-16 on some media and ANSI on others.
    const QByteArray bytes = file.readAll();
    const QString text = bytes.startsWith("\xff\xfe")
                             ? QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData() + 2),
                                                  (bytes.size() - 2) / 2)
                             : QString::fromUtf8(bytes);
    return parse(text);
}

}  // namespace lsw::host::provision::install
