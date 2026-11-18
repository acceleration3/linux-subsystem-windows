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

namespace lsw::host::services {
class StatusHub;
struct PriorityStatus;
}  // namespace lsw::host::services

namespace lsw::host::ui {
class InfoTip;
}

namespace lsw::host::manager {

class SettingControls;

class SettingsPage : public ui::ScrollPage {
    Q_OBJECT
public:
    SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                 services::StatusHub& status, config::LogVerbosity& verbosity,
                 QWidget* parent = nullptr);

    [[nodiscard]] static QString priority_about(const services::PriorityStatus& status);
    [[nodiscard]] static QString priority_note(const services::PriorityStatus& status);

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
    void refresh_clipboard_note();
    void refresh_priority_note();

    struct Row {
        ui::InfoTip* about = nullptr;
        QLabel* note = nullptr;
    };

    QVBoxLayout* new_group(const QString& title);
    Row add_row(QVBoxLayout* group, const QString& key, const QString& label, const QString& about,
                QWidget* control);

    config::PendingChanges& pending_;
    services::StatusHub& status_;
    config::LogVerbosity& verbosity_;
    SettingControls* controls_ = nullptr;

    Row priority_row_;
    Row clipboard_row_;
    QPushButton* debug_button_ = nullptr;
    QPushButton* debug_stop_ = nullptr;
    QTimer debug_clock_;
};

}
