#pragma once

#include "lsw/host/ui/scroll_page.hpp"

namespace lsw::host::ui { class FlowLayout; }
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

class MaintenancePage : public ui::ScrollPage {
    Q_OBJECT
public:
    explicit MaintenancePage(services::StatusHub& status, QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void summary_changed();
    void update_requested();
    void check_requested();
    void download_requested();
    void trim_requested();
    void reinstall_requested();

private:
    void build_release();
    void build_update();
    void build_tools();
    void refresh();
    void refresh_release();

    services::StatusHub& status_;
    QLabel* release_pill_ = nullptr;
    QPushButton* copy_button_ = nullptr;
    QPushButton* check_button_ = nullptr;
    QVBoxLayout* release_box_ = nullptr;
    QLabel* pill_ = nullptr;
    QPushButton* update_button_ = nullptr;
    QVBoxLayout* facts_box_ = nullptr;
    QWidget* facts_ = nullptr;
    ui::FlowLayout* chips_ = nullptr;
    QWidget* chip_row_ = nullptr;
};

}
