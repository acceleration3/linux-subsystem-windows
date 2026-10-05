#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace lsw::host::apps {
class GuestAppLauncher;
class GuestFileTransfer;
}  // namespace lsw::host::apps

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::manager {

class RunOnVmDialog : public QDialog {
    Q_OBJECT
public:
    RunOnVmDialog(link::AgentRpcClient& rpc, apps::GuestAppLauncher& launcher,
                  QWidget* parent = nullptr);
    void set_agent_online(bool online);
    void reject() override;

private:
    void browse();
    void run();
    void set_busy(bool busy);
    void show_error(const QString& error);

    apps::GuestAppLauncher& launcher_;
    apps::GuestFileTransfer* transfer_ = nullptr;
    QLineEdit* path_ = nullptr;
    QLineEdit* args_ = nullptr;
    QPushButton* browse_ = nullptr;
    QPushButton* run_ = nullptr;
    QPushButton* close_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QString guest_path_;
    bool busy_ = false;
    bool online_ = true;
    bool closing_ = false;
};

}  // namespace lsw::host::manager
