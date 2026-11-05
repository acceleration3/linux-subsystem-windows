#pragma once

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace lsw::host::services { class StatusHub; }

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
    [[nodiscard]] QString signature() const;

    [[nodiscard]] QWidget* build_warnings();
    [[nodiscard]] QWidget* build_adapters() const;
    [[nodiscard]] QWidget* build_guest_driver() const;
    [[nodiscard]] QWidget* build_log();
    void open_log() const;

    services::StatusHub& status_;
    GpuStatusAlerts& alerts_;
    QVBoxLayout* content_ = nullptr;
    QLabel* log_meta_ = nullptr;
    QLabel* log_text_ = nullptr;
    QString built_for_;
};

}
