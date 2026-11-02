#pragma once

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace lsw::host::services {
class DriverCatalog : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(const QByteArray& page, const QString& error)>;
    using Fetch = std::function<void(const QUrl& url, Done done)>;

    explicit DriverCatalog(Fetch fetch = {}, QString cache_path = {}, QObject* parent = nullptr);

    [[nodiscard]] static std::optional<QString> newest(const QByteArray& page);

    [[nodiscard]] static QUrl search_url(const QString& vendor, const QString& device);

    void lookup(const QString& adapter_names);

Q_SIGNALS:
    void completed(const QString& versioned);

private:
    void answer(const QString& key, const QByteArray& page, const QString& error);
    void complete();
    void load_cache();
    void save_cache() const;

    Fetch fetch_;
    QNetworkAccessManager nam_;
    QString names_;
    QHash<QString, bool> pending_;
    QHash<QString, QString> versions_;
    QString cache_path_;
    QHash<QString, QString> cached_;
    bool started_ = false;
    bool completed_ = false;
};

}
