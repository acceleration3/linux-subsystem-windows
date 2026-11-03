#pragma once

#include "lsw/host/ui/scroll_page.hpp"
#include "lsw/host/ui/segmented_control.hpp"

#include <QStringList>

class QLabel;
class QVBoxLayout;

namespace lsw::host::config {
class ManagerSettings;
class PendingChanges;
}
namespace lsw::host::services { class StatusHub; }
namespace lsw::host::ui { class MonitorSketch; }

namespace lsw::host::manager {

class ResolutionEditor;
class SettingControls;

class DisplayPage : public ui::ScrollPage {
    Q_OBJECT
public:
    DisplayPage(services::StatusHub& status, config::ManagerSettings& settings,
                config::PendingChanges& pending, QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;

    void refresh();

Q_SIGNALS:
    void summary_changed();

protected:
    void showEvent(QShowEvent* event) override;

private:
    struct Rate {
        int hertz = 0;
        quint32 millihz = 0;
        QString label;
        QStringList monitors;
    };

    void build_now();
    void build_resolutions();
    void build_refresh_and_scale();

    void refresh_now();
    void refresh_resolutions();
    void refresh_refresh_and_scale();

    [[nodiscard]] QVector<Rate> rates() const;
    [[nodiscard]] QString refresh_note(const QVector<Rate>& rates, qsizetype shown) const;
    void choose_refresh(const QVariant& value, const QVector<Rate>& hidden);

    services::StatusHub& status_;
    config::ManagerSettings& settings_;
    SettingControls* controls_ = nullptr;

    QLabel* live_pill_ = nullptr;
    ui::MonitorSketch* sketch_ = nullptr;
    QVBoxLayout* facts_box_ = nullptr;
    QWidget* facts_ = nullptr;
    QStringList facts_signature_;

    ui::SegmentedControl* source_ = nullptr;
    QWidget* list_box_ = nullptr;
    QWidget* host_box_ = nullptr;
    QVBoxLayout* host_column_ = nullptr;
    QWidget* host_facts_ = nullptr;
    ResolutionEditor* editor_ = nullptr;

    ui::SegmentedControl* refresh_seg_ = nullptr;
    ui::SegmentedControl* scale_seg_ = nullptr;
    QLabel* refresh_meta_ = nullptr;
    QVector<Rate> hidden_rates_;
};

}
