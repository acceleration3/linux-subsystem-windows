#pragma once

#include "lsw/host/config/manager_settings.hpp"

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <optional>

namespace lsw::host::config {
class PendingChanges : public QObject {
    Q_OBJECT
public:
    explicit PendingChanges(ManagerSettings& settings, QObject* parent = nullptr);

    bool stage(const QString& key, const QVariant& value);

    [[nodiscard]] QVariant staged_or_current(const QString& key) const;

    [[nodiscard]] int count() const noexcept { return static_cast<int>(staged_.size()); }

    [[nodiscard]] bool empty() const noexcept { return staged_.isEmpty(); }

    [[nodiscard]] QStringList summary() const;

    std::optional<HostShare> add_share(const QString& folder);

    void commit();

    void revert();

Q_SIGNALS:
    void changed();

private:
    ManagerSettings& settings_;
    QMap<QString, QVariant> staged_;
};

}
