#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QWidget>

class QComboBox;
class QTableWidget;

namespace lsw::host::config { class PendingChanges; }
namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {
class AdapterRoles : public QWidget {
    Q_OBJECT
public:
    AdapterRoles(config::PendingChanges& pending, const services::StatusHub& status,
                 QWidget* parent = nullptr);

    [[nodiscard]] static QVector<platform::HostGpu>
    listed(const QVector<platform::HostGpu>& gpus, const QString& display_uuid,
           const QStringList& render);

private:
    void rebuild();
    void choose(const QString& uuid, int role);
    void set_role_cell(int row, QComboBox* combo);

    config::PendingChanges& pending_;
    const services::StatusHub& status_;
    QVector<platform::HostGpu> gpus_;
    bool probed_ = false;
    QTableWidget* table_ = nullptr;
};

}
