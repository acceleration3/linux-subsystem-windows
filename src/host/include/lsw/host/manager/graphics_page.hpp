#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace lsw::host::config {
class ManagerSettings;
class PendingChanges;
}
namespace lsw::host::platform { struct HostGpu; }
namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

class AdapterRoles;
class GraphicsAlerts;
class SettingControls;

class GraphicsPage : public QWidget {
    Q_OBJECT
public:
    GraphicsPage(services::StatusHub& status, GraphicsAlerts& alerts,
                 config::ManagerSettings& settings, config::PendingChanges& pending,
                 QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;
    [[nodiscard]] QStringList status_facts() const;

Q_SIGNALS:
    void summary_changed();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void rebuild();
    [[nodiscard]] QString signature() const;

    [[nodiscard]] QWidget* build_warnings();
    [[nodiscard]] QWidget* build_adapter_card(const platform::HostGpu& gpu);
    [[nodiscard]] QWidget* build_apis() const;
    [[nodiscard]] QWidget* build_device();

    services::StatusHub& status_;
    GraphicsAlerts& alerts_;
    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
    AdapterRoles* roles_ = nullptr;
    SettingControls* controls_ = nullptr;
    QVBoxLayout* content_ = nullptr;
    QString built_for_;
};

}
