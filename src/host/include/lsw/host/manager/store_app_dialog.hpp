#pragma once

#include "lsw/host/apps/app_config.hpp"

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace lsw::host::link {
class AgentRpcClient;
struct AgentReply;
}

namespace lsw::host::manager {
class StoreAppDialog : public QDialog {
    Q_OBJECT
public:
    explicit StoreAppDialog(link::AgentRpcClient& rpc, QWidget* parent = nullptr);

    [[nodiscard]] const apps::AppConfig& chosen() const noexcept { return chosen_; }

private:
    class RowDelegate;
    struct StoreApp {
        QString aumid;
        QString name;
        QString family;
        QString icon_png_base64;
    };

    void read(const link::AgentReply& reply);
    void filter();
    void show_list();
    void show_empty(QWidget* mark, const QString& text);
    void choose();

    QLineEdit* search_;
    QStackedWidget* pages_;
    QListWidget* list_;
    QLabel* reading_;
    QLabel* offline_;
    QLabel* empty_text_;
    QLabel* count_;
    QPushButton* add_;
    QVector<StoreApp> apps_;
    apps::AppConfig chosen_;
};

}
