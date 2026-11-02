#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class QProcess;

namespace lsw::host::platform {
class UsbAccess : public QObject {
    Q_OBJECT
public:
    explicit UsbAccess(QObject* parent = nullptr);

    [[nodiscard]] static QString helper_path();

    bool grant(int vendor, int product);

    [[nodiscard]] bool granting(int vendor, int product) const;

Q_SIGNALS:
    void finished(int vendor, int product, bool ok, const QString& error);

private:
    [[nodiscard]] static QString key(int vendor, int product);
    void done(const QString& key, int vendor, int product, bool ok, const QString& error);

    QSet<QString> granting_;
};

}
