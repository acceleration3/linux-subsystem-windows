#include "lsw/host/provision/install/install_image.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/provision/install/seven_zip.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace lsw::host::provision::install {

namespace {
constexpr int kExtractTimeoutMs = 900000;

const QRegularExpression& swm_re() {
    static const QRegularExpression re{
        QStringLiteral(R"(^sources/install(\d*)\.swm$)"),
        QRegularExpression::CaseInsensitiveOption};
    return re;
}

[[nodiscard]] QString find_single_file(const QStringList& entries) {
    for (const QString& wanted :
         {QStringLiteral("sources/install.wim"), QStringLiteral("sources/install.esd")}) {
        for (const QString& entry : entries) {
            if (entry.compare(wanted, Qt::CaseInsensitive) == 0) {
                return entry;
            }
        }
    }
    return {};
}

[[nodiscard]] QStringList find_swm_parts(const QStringList& entries) {
    QList<QPair<int, QString>> found;
    for (const QString& entry : entries) {
        const QRegularExpressionMatch match = swm_re().match(entry);
        if (!match.hasMatch()) {
            continue;
        }
        const QString digits = match.captured(1);
        found.append({digits.isEmpty() ? 1 : digits.toInt(), entry});
    }
    std::ranges::sort(found, [](const auto& a, const auto& b) { return a.first < b.first; });

    QStringList ordered;
    ordered.reserve(found.size());
    for (const auto& [number, entry] : found) {
        ordered.append(entry);
    }
    return ordered;
}

[[nodiscard]] bool extract(const QString& iso_path, const QStringList& entries,
                           const QString& dir, QStringList* out, QString* error_out) {
    QStringList args{QStringLiteral("e"), QStringLiteral("-y"), QStringLiteral("-bso0"),
                     QStringLiteral("-bsp0"), iso_path};
    args.append(entries);
    args.append(QStringLiteral("-o%1").arg(dir));
    if (!run_seven_zip(args, kExtractTimeoutMs, error_out)) {
        return false;
    }

    out->clear();
    for (const QString& entry : entries) {
        const QString path = QDir(dir).filePath(QFileInfo(entry).fileName());
        if (!QFile::exists(path)) {
            if (error_out != nullptr) {
                *error_out = QStringLiteral("%1 did not come out of the image").arg(entry);
            }
            return false;
        }
        out->append(path);
    }
    return true;
}

}

bool stage_install_image(const QString& iso_path, const QString& dir, StagedInstallImage* out,
                         QString* error_out) {
    QString listing_error;
    const QStringList entries = seven_zip_entries(iso_path, &listing_error);
    if (entries.isEmpty()) {
        if (error_out != nullptr) {
            *error_out = listing_error.isEmpty()
                             ? QStringLiteral("%1 has nothing in it").arg(iso_path)
                             : QStringLiteral("could not read %1: %2")
                                   .arg(iso_path, listing_error);
        }
        return false;
    }

    QStringList wanted;
    if (const QString single = find_single_file(entries); !single.isEmpty()) {
        wanted = {single};
    } else {
        wanted = find_swm_parts(entries);
    }
    if (wanted.isEmpty()) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("%1 has no sources/install.wim, install.esd or "
                                        "install.swm, so it is not Windows installation media")
                             .arg(iso_path);
        }
        return false;
    }
    if (wanted.size() > 1) {
        LSW_INFO("install image: {} is split across {} parts", iso_path.toStdString(),
                 wanted.size());
    }

    QStringList staged;
    if (!extract(iso_path, wanted, dir, &staged, error_out)) {
        return false;
    }

    out->path = staged.takeFirst();
    out->extra_parts = staged;
    return true;
}

}
