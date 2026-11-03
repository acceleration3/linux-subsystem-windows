#include "lsw/host/services/guest_document_path.hpp"

#include "lsw/base/log.hpp"
#include "lsw/linkproto/file_name.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace lsw::host::services {

namespace {
QString root_of(const QString& folder) {
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(folder) : canonical;
}

}

bool GuestDocumentPath::within(const QString& path, const QString& root) {
    const QString folder = root_of(root);
    if (folder == QLatin1String("/")) {
        return true;
    }
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

bool GuestDocumentPath::legal_component(const QString& name) {
    return linkproto::WindowsFileName::legal(name.toStdString());
}

QString GuestDocumentPath::canonical(const QString& path) {
    return QFileInfo(path).canonicalFilePath();
}

std::optional<QString> GuestDocumentPath::map(const QString& host_path,
                                              const QVector<vm::DomainDescription::Share>& shares) {
    const vm::DomainDescription::Share* best = nullptr;
    QString best_root;
    for (const auto& share : shares) {
        if (share.drive.size() != 1 || !share.drive.at(0).isLetter() || share.host_path.isEmpty()) {
            continue;
        }
        const QString root = root_of(share.host_path);
        if (within(host_path, root) && (best == nullptr || root.size() > best_root.size())) {
            best = &share;
            best_root = root;
        }
    }
    if (best == nullptr) {
        LSW_DEBUG("document path: {} is under no share", host_path.toStdString());
        return std::nullopt;
    }
    const QString relative =
        best_root == QLatin1String("/") ? host_path.mid(1) : host_path.mid(best_root.size() + 1);
    QString guest = best->drive.toUpper() + QStringLiteral(":\\");
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (!legal_component(parts.at(i))) {
            LSW_DEBUG("document path: {} cannot be a Windows name", parts.at(i).toStdString());
            return std::nullopt;
        }
        guest += (i == 0 ? QString{} : QStringLiteral("\\")) + parts.at(i);
    }
    LSW_DEBUG("document path: {} is {}", host_path.toStdString(), guest.toStdString());
    return guest;
}

}
