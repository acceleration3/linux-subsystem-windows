#pragma once

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/ui/scroll_page.hpp"

class QLabel;

namespace lsw::host::config { class PendingChanges; }
namespace lsw::host::services { class StatusHub; }
namespace lsw::host::ui {
class ItemList;
class SegmentedControl;
}

namespace lsw::host::manager {

class SettingControls;

class FilesPage : public ui::ScrollPage {
    Q_OBJECT
public:
    FilesPage(services::StatusHub& status, config::ManagerSettings& settings,
              config::PendingChanges& pending, QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void summary_changed();

private:
    void build_folders();
    void build_smb();
    void refresh_folders();
    void refresh_smb();
    void add_folder();

    struct Entry {
        config::HostShare share;
        bool leaving = false;
        bool defined = false;
    };

    [[nodiscard]] QVector<Entry> entries() const;
    [[nodiscard]] bool in_windows(const config::HostShare& share) const;
    [[nodiscard]] QVector<config::HostShare> staged_shares() const;
    void stage_shares(const QVector<config::HostShare>& shares);

    services::StatusHub& status_;
    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;
    SettingControls* controls_ = nullptr;

    QStringList rows_signature_;
    QLabel* letters_ = nullptr;
    QWidget* add_button_ = nullptr;
    ui::ItemList* list_ = nullptr;
    QLabel* empty_ = nullptr;

    QLabel* serving_ = nullptr;
    QLabel* address_ = nullptr;
};

}
