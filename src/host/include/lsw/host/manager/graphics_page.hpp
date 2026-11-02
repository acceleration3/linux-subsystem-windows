#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace lsw::host::config { class ManagerSettings; }
namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

class GraphicsAlerts;

class GraphicsPage : public QWidget {
    Q_OBJECT
public:
    GraphicsPage(services::StatusHub& status, GraphicsAlerts& alerts,
                 config::ManagerSettings& settings, QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;
    [[nodiscard]] QStringList status_facts() const;

Q_SIGNALS:
    void settings_requested();
    void summary_changed();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void rebuild();
    void update_log();
    [[nodiscard]] QString signature() const;

    [[nodiscard]] QWidget* build_warnings();
    [[nodiscard]] QWidget* build_adapter_card(int index) const;
    [[nodiscard]] QWidget* build_apis() const;
    [[nodiscard]] QWidget* build_device();
    [[nodiscard]] QWidget* build_guest_driver() const;
    [[nodiscard]] QWidget* build_log();
    void open_log() const;

    services::StatusHub& status_;
    GraphicsAlerts& alerts_;
    config::ManagerSettings& settings_;
    QVBoxLayout* content_ = nullptr;
    QLabel* log_meta_ = nullptr;
    QLabel* log_text_ = nullptr;
    QString built_for_;
};

}
