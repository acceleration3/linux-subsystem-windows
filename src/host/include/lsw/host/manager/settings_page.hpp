#pragma once

#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/ui/machine_limits.hpp"

#include <QHash>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QTableWidget;

namespace lsw::host::services { class StatusHub; }

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
    // ``status`` has the host's Vulkan devices, for the adapter table.
    SettingsPage(config::ManagerSettings& settings, config::PendingChanges& pending,
                 apps::AppConfigStore& apps, services::StatusHub& status,
                 QWidget* parent = nullptr);

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

    // A group is a titled block of rows; a row is a label, an
    // explanation, a now-or-restart tag, and a control.
    QVBoxLayout* new_group(const QString& title);
    // The tag follows the key unless ``tag`` says otherwise: "restart"
    // or "now", for a row whose effect and storage differ (the DPI is
    // written at once and read at the next logon).
    QWidget* add_row(QVBoxLayout* group, const QString& key, const QString& label,
                     const QString& description, QWidget* control, const char* tag = nullptr);
    // A row whose control is too wide for the control column: the name
    // and the explanation, then ``control`` the full width under them.
    void add_wide_row(QVBoxLayout* group, bool restart, const QString& label,
                      const QString& description, QWidget* control);
    // A row whose control is one button that leads somewhere else:
    // ``clicked`` is the signal it emits. ``role`` dresses the button.
    void add_action_row(QVBoxLayout* group, const QString& label, const QString& description,
                        const QString& button_text, void (SettingsPage::*clicked)(),
                        const char* role = nullptr);

    // Controls, each wired to write or stage on its own.
    QWidget* make_slider(const QString& key, const ui::MachineLimits::Range& range,
                         const QString& suffix);
    QWidget* make_choice(const QString& key, const QVector<QPair<QString, QVariant>>& options);

    QWidget* make_toggle(const QString& key);
    QWidget* make_text(const QString& key);

    // Each control shows the staged value, or the stored one: Revert,
    // Apply, and a change made somewhere else (the Graphics page's
    // resolution switch, another manager) all move what it shows.
    void follow(const QString& key, std::function<void()> show);
    void show_current(const QString& key);

    // Writes a live key straight through, or stages a restart key.
    void put(const QString& key, const QVariant& value);

    // The shared folders as they will be after the next Apply: the
    // staged list if there is one, the stored one otherwise. Every edit
    // starts from this, so a second folder added before Apply keeps the
    // first.
    [[nodiscard]] QVector<config::HostShare> staged_shares() const;
    void stage_shares(const QVector<config::HostShare>& shares);

    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
    apps::AppConfigStore& apps_;
    services::StatusHub& status_;

    QVBoxLayout* content_ = nullptr;
    QWidget* apply_bar_ = nullptr;
    QLabel* apply_count_ = nullptr;
    QLabel* apply_diffs_ = nullptr;
    QTableWidget* shares_table_ = nullptr;
    QHash<QString, std::function<void()>> shows_;
};

}  // namespace lsw::host::manager
