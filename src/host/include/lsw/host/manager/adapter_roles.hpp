#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QObject>

class QComboBox;

namespace lsw::host::config { class PendingChanges; }
namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {
class AdapterRoles : public QObject {
    Q_OBJECT
public:
    enum class Role { Display = 0, RenderOnly = 1, NotUsed = 2 };

    AdapterRoles(config::PendingChanges& pending, const services::StatusHub& status,
                 QObject* parent = nullptr);

    [[nodiscard]] QVector<platform::HostGpu> listed() const;

    [[nodiscard]] Role staged(const QString& uuid) const;
    [[nodiscard]] Role running(const QString& uuid) const;
    [[nodiscard]] bool differs(const QString& uuid) const;
    [[nodiscard]] int staged_render_count() const;

    [[nodiscard]] QComboBox* combo(const platform::HostGpu& gpu);

    [[nodiscard]] static QString label(Role role);

private:
    [[nodiscard]] QString staged_display_uuid() const;
    void choose(const QString& uuid, Role role);

    config::PendingChanges& pending_;
    const services::StatusHub& status_;
};

}
