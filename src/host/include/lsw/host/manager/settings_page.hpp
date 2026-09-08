#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QTableWidget;

namespace lsw::host::manager {

// The settings page.
//
// Every row says whether it takes effect now or at the next boot, and
// the two behave differently: a live row writes through the moment it
// changes, a restart row stages into PendingChanges and shows up in the
// amber bar at the foot of the page. The guest is only ever restarted
// by the button in that bar.
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                 apps::AppConfigStore& apps, QWidget* parent = nullptr);

Q_SIGNALS:
    // The apply bar. The window owns the guest, so it does the stopping
    // and starting; this page only knows that the values are current.
    void restart_requested();
    // Buttons that lead somewhere else.
    void download_tool_requested();
    void trim_tool_requested();
    void reinstall_requested();
    void open_share_requested();
    void rewrite_entries_requested();

private:
    void build();
    void refresh_apply_bar();
    void rebuild_shares_table();
    void rebuild_bindings_table();

    // A group is a titled block of rows; a row is a label, an
    // explanation, a now-or-restart tag, and a control.
    QVBoxLayout* new_group(const QString& title);
    QWidget* add_row(QVBoxLayout* group, const QString& key, const QString& label,
                     const QString& description, QWidget* control);

    // Controls, each wired to write or stage on its own.
    QWidget* make_slider(const QString& key, int min, int max, int step,
                         const QString& suffix);
    QWidget* make_choice(const QString& key, const QVector<QPair<QString, QVariant>>& options);
    QWidget* make_toggle(const QString& key);
    QWidget* make_number(const QString& key, int min, int max);
    QWidget* make_text(const QString& key);

    // Writes a live key straight through, or stages a restart key.
    void put(const QString& key, const QVariant& value);

    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
    apps::AppConfigStore& apps_;

    QVBoxLayout* content_ = nullptr;
    QWidget* apply_bar_ = nullptr;
    QLabel* apply_count_ = nullptr;
    QLabel* apply_diffs_ = nullptr;
    QTableWidget* shares_table_ = nullptr;
    QTableWidget* bindings_table_ = nullptr;
};

}  // namespace lsw::host::manager
