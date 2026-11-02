#pragma once

#include "lsw/host/vm/domain_description.hpp"

#include <QString>
#include <QVector>
#include <optional>

namespace lsw::host::services {
class GuestDocumentPath {
public:
    GuestDocumentPath() = delete;

    [[nodiscard]] static std::optional<QString>
    map(const QString& host_path, const QVector<vm::DomainDescription::Share>& shares);

    [[nodiscard]] static QString canonical(const QString& path);

    [[nodiscard]] static bool within(const QString& path, const QString& root);

    [[nodiscard]] static bool legal_component(const QString& name);
};

}
