#include "lsw/host/services/guest_document_path.hpp"

#include "lsw/linkproto/file_name.hpp"

#include <QFileInfo>
#include <QStringList>

namespace lsw::host::services {

bool GuestDocumentPath::within(const QString& path, const QString& root) {
    if (root == QLatin1String("/")) {
        return true;
    }
    return path == root || path.startsWith(root + QLatin1Char('/'));
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
    for (const auto& share : shares) {
        if (share.tag.size() != 1 || !share.tag.at(0).isLetter() || share.host_path.isEmpty()) {
            continue;
        }
        if (within(host_path, share.host_path)
            && (best == nullptr || share.host_path.size() > best->host_path.size())) {
            best = &share;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    const QString relative =
        best->host_path == QLatin1String("/") ? host_path.mid(1) : host_path.mid(best->host_path.size() + 1);
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
