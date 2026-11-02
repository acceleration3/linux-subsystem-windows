#pragma once

#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/catalog.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace lsw::host::apps {
class AppConfigStore : public QObject {
    Q_OBJECT
public:
    explicit AppConfigStore(QString dir, QObject* parent = nullptr);

    void reload();

    [[nodiscard]] const QList<AppConfig>& configs() const noexcept { return configs_; }

    [[nodiscard]] AppConfig* find(const QString& id);
    [[nodiscard]] const AppConfig* find(const QString& id) const;

    void upsert(AppConfig cfg);

    void remove(const QString& id);

    void merge_from_agent(const QList<AppEntry>& entries);

    void set_store_extensions(const QHash<QString, QStringList>& extensions);

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] QString file_for(const QString& id) const;
    bool write_file(const AppConfig& cfg);

    QString dir_;
    QList<AppConfig> configs_;
};

}
