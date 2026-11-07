#pragma once

#include <QVector>
#include <QWidget>

class QLabel;
class QTableWidget;
class QVBoxLayout;

namespace lsw::host::services { class StatusHub; }
namespace lsw::host::ui { class UsageGraph; }

namespace lsw::host::manager {

class GpuStatusAlerts;

class GpuStatusPage : public QWidget {
    Q_OBJECT
public:
    GpuStatusPage(services::StatusHub& status, GpuStatusAlerts& alerts, QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void summary_changed();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void rebuild();
    void update_log();
    void update_window();
    void fill_apps();
    [[nodiscard]] QString signature() const;

    [[nodiscard]] QWidget* build_warnings();
    [[nodiscard]] QWidget* build_adapters() const;
    [[nodiscard]] QWidget* build_guest_driver() const;
    [[nodiscard]] QWidget* build_host_window();
    [[nodiscard]] QWidget* build_guest_vram();
    [[nodiscard]] QWidget* build_apps();
    [[nodiscard]] QWidget* build_log();
    void open_log() const;

    services::StatusHub& status_;
    GpuStatusAlerts& alerts_;
    QVBoxLayout* content_ = nullptr;
    QLabel* log_meta_ = nullptr;
    QLabel* log_text_ = nullptr;
    ui::UsageGraph* graph_ = nullptr;
    QLabel* window_use_ = nullptr;
    QLabel* window_peak_ = nullptr;
    QVector<QLabel*> vram_cap_;
    QVector<QLabel*> vram_reserve_;
    QTableWidget* apps_table_ = nullptr;
    QLabel* apps_note_ = nullptr;
    QString built_for_;
};

}
