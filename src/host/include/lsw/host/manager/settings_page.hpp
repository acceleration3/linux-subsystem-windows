#pragma once

#include "lsw/host/platform/host_bridges.hpp"
#include "lsw/host/ui/scroll_page.hpp"

#include <QTimer>

class QComboBox;
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
}

namespace lsw::host::manager {

class PortForwardEditor;
class SettingControls;

class SettingsPage : public ui::ScrollPage {
    Q_OBJECT
public:
    SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                 services::StatusHub& status, config::LogVerbosity& verbosity,
                 QWidget* parent = nullptr);

    [[nodiscard]] static QString priority_note(const services::PriorityStatus& status);
    [[nodiscard]] static QString bridge_note(bool bridged, const QString& bridge,
                                             const platform::HostBridges::Facts& facts);

Q_SIGNALS:
    void rewrite_entries_requested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void build_machine();
    void build_network(config::ManagerSettings& settings);
    void build_startup();
    void build_desktop();
    void build_logging();
    void refresh_debug_row();
    void refresh_clipboard_note();
    void refresh_priority_note();
    void refresh_network();

    QVBoxLayout* new_group(const QString& title);
    QWidget* add_row(QVBoxLayout* group, const QString& key, const QString& label,
                     const QString& description, QWidget* control);

    config::PendingChanges& pending_;
    services::StatusHub& status_;
    config::LogVerbosity& verbosity_;
    SettingControls* controls_ = nullptr;

    QLabel* priority_note_ = nullptr;
    QLabel* clipboard_note_ = nullptr;
    QComboBox* bridge_ = nullptr;
    QLabel* bridge_note_ = nullptr;
    PortForwardEditor* forwards_ = nullptr;
    QPushButton* debug_button_ = nullptr;
    QPushButton* debug_stop_ = nullptr;
    QTimer debug_clock_;
};

}
