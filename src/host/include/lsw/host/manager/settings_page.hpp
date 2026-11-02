#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/platform/host_monitor_rates.hpp"
#include "lsw/host/ui/machine_limits.hpp"

#include <QHash>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QTableWidget;

namespace lsw::host::config { class LogVerbosity; }

namespace lsw::host::services {
class StatusHub;
class UsbPassthrough;
}

namespace lsw::host::manager {
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                 apps::AppConfigStore& apps, services::StatusHub& status,
                 services::UsbPassthrough& usb, config::LogVerbosity& verbosity,
                 QWidget* parent = nullptr);

Q_SIGNALS:
    void restart_requested();
    void download_tool_requested();
    void trim_tool_requested();
    void reinstall_requested();
    void update_requested();
    void open_share_requested();
    void rewrite_entries_requested();
    void open_log_folder_requested();
    void collect_logs_requested();
    void clear_logs_requested();

public:
    void set_logs_busy(bool busy);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void build();
    void refresh_apply_bar();
    void rebuild_shares_table();
    void refresh_update_row();
    void refresh_mount_row();
    void build_logs_group();
    void refresh_debug_row();

    QVBoxLayout* new_group(const QString& title);
    QWidget* add_row(QVBoxLayout* group, const QString& key, const QString& label,
                     const QString& description, QWidget* control);
    void add_wide_row(QVBoxLayout* group, bool restart, const QString& label,
                      const QString& description, QWidget* control);
    void add_action_row(QVBoxLayout* group, const QString& label, const QString& description,
                        const QString& button_text, void (SettingsPage::*clicked)(),
                        const char* role = nullptr);

    QWidget* make_slider(const QString& key, const ui::MachineLimits::Range& range,
                         const QString& suffix);
    QWidget* make_choice(const QString& key, const QVector<QPair<QString, QVariant>>& options);

    QWidget* make_toggle(const QString& key);
    QWidget* make_text(const QString& key);
    void add_refresh_row(QVBoxLayout* group);
    [[nodiscard]] static QString refresh_description(const QVector<platform::RateChoice>& rates);

    void follow(const QString& key, std::function<void()> show);
    void show_current(const QString& key);

    void put(const QString& key, const QVariant& value);

    [[nodiscard]] QVector<config::HostShare> staged_shares() const;
    void stage_shares(const QVector<config::HostShare>& shares);

    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
    apps::AppConfigStore& apps_;
    services::StatusHub& status_;
    services::UsbPassthrough& usb_;
    config::LogVerbosity& verbosity_;

    QVBoxLayout* content_ = nullptr;
    QWidget* apply_bar_ = nullptr;
    QLabel* apply_count_ = nullptr;
    QLabel* apply_diffs_ = nullptr;
    QTableWidget* shares_table_ = nullptr;
    QLabel* update_note_ = nullptr;
    QLabel* mount_note_ = nullptr;
    QPushButton* mount_button_ = nullptr;
    QPushButton* update_button_ = nullptr;
    QPushButton* debug_button_ = nullptr;
    QPushButton* debug_stop_ = nullptr;
    QPushButton* clear_logs_button_ = nullptr;
    QTimer debug_clock_;
    QHash<QString, std::function<void()>> shows_;
};

}
