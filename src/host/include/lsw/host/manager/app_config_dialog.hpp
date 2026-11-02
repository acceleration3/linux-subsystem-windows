#pragma once

#include "lsw/host/apps/app_config.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::manager {
class AppConfigDialog : public QDialog {
    Q_OBJECT
public:
    AppConfigDialog(apps::AppConfig initial, link::AgentRpcClient& rpc,
                    QWidget* parent = nullptr);

    [[nodiscard]] apps::AppConfig result() const { return cfg_; }

    [[nodiscard]] bool removal_requested() const noexcept { return removal_requested_; }

private Q_SLOTS:
    void on_path_changed(const QString& text);
    void on_path_idle();
    void on_accept();
    void fetch_icon();
    void fetch_store_icon();

private:
    void update_icon_preview();
    void make_default();

    apps::AppConfig cfg_;
    link::AgentRpcClient& rpc_;

    QLineEdit* name_edit_ = nullptr;
    QLineEdit* path_edit_ = nullptr;
    QLineEdit* cmdline_edit_ = nullptr;
    QLineEdit* workdir_edit_ = nullptr;
    QPlainTextEdit* env_edit_ = nullptr;
    QCheckBox* publish_box_ = nullptr;
    QLineEdit* mime_edit_ = nullptr;
    QPushButton* make_default_ = nullptr;
    QLabel* identity_name_ = nullptr;
    bool removal_requested_ = false;
    QLabel* icon_label_ = nullptr;
    QLabel* icon_status_ = nullptr;
    QTimer path_idle_timer_;
};

}
