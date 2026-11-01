#include "lsw/host/services/guest_document_path.hpp"

#include "lsw/linkproto/file_name.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace lsw::host::services {

namespace {

// A share's folder as a document's canonical path would spell it. The folder
// dialog can hand back a trailing slash, and the folder itself can be reached
// through a symlink; one that is not there has only its spelling to go by.
QString root_of(const QString& folder) {
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(folder) : canonical;
}

}  // namespace

bool GuestDocumentPath::within(const QString& path, const QString& root) {
    const QString folder = root_of(root);
    if (folder == QLatin1String("/")) {
        return true;
    }
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

bool GuestDocumentPath::legal_component(const QString& name) {
    // The one rule, shared with the agent (linkproto/file_name.hpp).
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
        if (share.tag.size() != 1 || !share.tag.at(0).isLetter() || share.host_path.isEmpty()) {
            continue;
        }
        const QString root = root_of(share.host_path);
        if (within(host_path, root) && (best == nullptr || root.size() > best_root.size())) {
            best = &share;
            best_root = root;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    const QString relative =
        best_root == QLatin1String("/") ? host_path.mid(1) : host_path.mid(best_root.size() + 1);
    QString guest = best->tag.toUpper() + QStringLiteral(":\\");
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (!legal_component(parts.at(i))) {
            return std::nullopt;
        }
        guest += (i == 0 ? QString{} : QStringLiteral("\\")) + parts.at(i);
    }
    return guest;
}

}  // namespace lsw::host::services
