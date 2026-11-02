#pragma once

#include "lsw/host/manager/guest_restart.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace lsw::host::services {
class HostServices;
}

namespace lsw::host::manager {

class GuestAppLaunchController;

class DocumentOpener : public QObject {
    Q_OBJECT
public:
    using Restarter = std::function<RestartResult(QString* reason)>;

    DocumentOpener(services::HostServices& services, GuestAppLaunchController& controller,
                   QObject* parent = nullptr);

    void set_restarter(Restarter restarter) { restarter_ = std::move(restarter); }

    void open(const QString& app_id, const QStringList& files, const QString& activation_token);

private:
    struct Plan {
        QStringList mapped;
        QStringList unmapped;
        QVector<vm::DomainDescription::Share> shares;
    };

    void proceed(const QString& app_id, const Plan& plan, const QString& activation_token);
    void ask(const QString& app_id, const Plan& plan, const QString& activation_token);
    [[nodiscard]] QStringList share_and_map(const QStringList& shareable, const Plan& plan,
                                            QStringList* uploads);

    services::HostServices& services_;
    GuestAppLaunchController& controller_;
    Restarter restarter_;
};

}
