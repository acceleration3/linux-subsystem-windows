#pragma once

#include "lsw/host/ui/scroll_page.hpp"

#include <QTimer>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace lsw::host::config {
class LogVerbosity;
class ManagerSettings;
class PendingChanges;
}
namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

class SettingControls;

class SettingsPage : public ui::ScrollPage {
    Q_OBJECT
public:
    SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                 services::StatusHub& status, config::LogVerbosity& verbosity,
                 QWidget* parent = nullptr);

Q_SIGNALS:
    void rewrite_entries_requested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void build_machine();
    void build_startup();
    void build_desktop();
    void build_logging();
    void refresh_debug_row();

    QVBoxLayout* new_group(const QString& title);
    QWidget* add_row(QVBoxLayout* group, const QString& key, const QString& label,
                     const QString& description, QWidget* control);

    config::PendingChanges& pending_;
    services::StatusHub& status_;
    config::LogVerbosity& verbosity_;
    SettingControls* controls_ = nullptr;

    QPushButton* debug_button_ = nullptr;
    QPushButton* debug_stop_ = nullptr;
    QTimer debug_clock_;
};

}
