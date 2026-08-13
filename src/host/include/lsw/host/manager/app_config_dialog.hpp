#pragma once

#include "lsw/host/apps/app_config.hpp"

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {

// Modal "configure application" dialog. Used both for editing an
// existing config and creating a new one. The icon preview auto-
// refreshes when the path field stops changing for ~600ms — the
// fetch goes out to the guest agent's ``get_icon`` RPC over the
// agent connection, so it only works when the VM is running.
class AppConfigDialog : public QDialog {
    Q_OBJECT
public:
    AppConfigDialog(apps::AppConfig initial, link::AgentRpcClient& rpc,
                    QWidget* parent = nullptr);

    [[nodiscard]] apps::AppConfig result() const { return cfg_; }

    // Capture method the dialog was opened with. AppListWidget reads
    // this after ``exec()`` to decide whether to push a
    // ``set_capture_method`` RPC.
    [[nodiscard]] apps::CaptureMethod initial_capture() const { return initial_capture_; }

private Q_SLOTS:
    void on_path_changed(const QString& text);
    void on_path_idle();
    void on_accept();
    void fetch_icon();

private:
    void update_icon_preview();

    apps::AppConfig cfg_;
    apps::CaptureMethod initial_capture_;
    link::AgentRpcClient& rpc_;

    QLineEdit* name_edit_ = nullptr;
    QLineEdit* path_edit_ = nullptr;
    QLineEdit* cmdline_edit_ = nullptr;
    QComboBox* capture_combo_ = nullptr;
    QLabel* icon_label_ = nullptr;
    QLabel* icon_status_ = nullptr;
    QTimer path_idle_timer_;
};

}  // namespace lsw::host::manager
