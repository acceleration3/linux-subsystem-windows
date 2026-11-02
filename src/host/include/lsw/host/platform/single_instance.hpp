#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace lsw::host::platform {
class SingleInstance : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.lsw.Manager.Lifecycle")
public:
    explicit SingleInstance(QObject* parent = nullptr);

    [[nodiscard]] bool acquire();

    void release();

    [[nodiscard]] bool raise_existing();

    [[nodiscard]] bool launch_app_existing(const QString& app_id, const QString& activation_token);

    [[nodiscard]] bool open_documents_existing(const QString& app_id, const QStringList& files,
                                               const QString& activation_token);

public Q_SLOTS:
    void raise();
    void launch_app(const QString& app_id, const QString& activation_token);
    void open_documents(const QString& app_id, const QStringList& files,
                        const QString& activation_token);

Q_SIGNALS:
    void raise_requested();
    void launch_app_requested(const QString& app_id, const QString& activation_token);
    void open_documents_requested(const QString& app_id, const QStringList& files,
                                  const QString& activation_token);

private:
    bool primary_ = false;
};

}
