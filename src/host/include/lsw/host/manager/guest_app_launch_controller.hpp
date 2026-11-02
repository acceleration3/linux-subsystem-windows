#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

class QIcon;

namespace lsw::host::services {
class HostServices;
}

namespace lsw::host::apps {
class GuestAppLauncher;
}

namespace lsw::host::manager {

class LaunchSplash;

class GuestAppLaunchController : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString app_id;
        QString target;
        QStringList documents;
        QStringList uploads;
        int restart_from_id = -1;
        QString leading_args;
        QString activation_token;

        [[nodiscard]] bool has_documents() const noexcept {
            return !documents.isEmpty() || !uploads.isEmpty();
        }
    };

    explicit GuestAppLaunchController(services::HostServices& services, QObject* parent = nullptr);

    void launch(const QString& app_id, const QString& activation_token);

    void open(Request request);

    void fail(const Request& request, const QString& reason);

Q_SIGNALS:
    void application_running();
    void log_requested();

private:
    struct Waiting {
        Request request;
        QString target;
    };

    void fire(const Request& request, const QString& target);
    void launch_application(const Request& request, const QString& target);
    void fire_documents(const Request& request, const QString& target);
    [[nodiscard]] bool shares_ready_for(const Request& request, const QString& target);
    void upload_documents(const Request& original, Request working, const QString& target,
                          QStringList remaining, QStringList failed, QStringList failed_files);
    void resolve_handlers(const Request& request, const QString& target,
                          const QStringList& upload_failures, const QStringList& failed_files);
    void launch_group(const Request& request, const QString& target);

    void start_vm_and_wait(const QUuid& uuid, const Waiting& first);

    void ensure_channel_target(const QUuid& uuid);
    void show_failure(const QString& target, const QString& error, const Request& request);
    [[nodiscard]] Request request_for(const QString& path, const QStringList& documents) const;
    void describe(const Request& request, const QString& target, QString* name, QIcon* icon) const;

    services::HostServices& services_;
    apps::GuestAppLauncher* launcher_ = nullptr;

    QVector<Waiting> waiting_;
    bool waiting_active_ = false;
    int restart_from_id_ = -1;
    QVector<QPointer<QObject>> upkeep_waiters_;
    QPointer<LaunchSplash> splash_;
    QString splash_path_;
};

}
