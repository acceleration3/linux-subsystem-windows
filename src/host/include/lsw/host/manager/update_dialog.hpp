#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(services::StatusHub& status, QWidget* parent = nullptr);

Q_SIGNALS:
    void retry_requested();

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    void refresh();
    void rebuild_host_steps();
    void rebuild_guest_steps();

    services::StatusHub& status_;
    bool was_running_ = false;
    int reached_ = -1;
    bool restart_seen_ = false;

    QLabel* line_ = nullptr;
    QWidget* host_steps_ = nullptr;
    QWidget* guest_box_ = nullptr;
    QWidget* guest_steps_ = nullptr;
    QLabel* failure_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* close_ = nullptr;
};

}
