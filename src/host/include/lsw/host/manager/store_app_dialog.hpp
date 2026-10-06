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
}  // namespace lsw::host::link

namespace lsw::host::manager {

// Add a store app: the packaged (Microsoft Store, MSIX) applications the
// guest's user has, with their icons, narrowed by a search, and the one
// picked made into an application entry. Such an application has no path
// to browse to; Windows starts it by its Application User Model ID.
class StoreAppDialog : public QDialog {
    Q_OBJECT
public:
    explicit StoreAppDialog(link::AgentRpcClient& rpc, QWidget* parent = nullptr);

    // The entry for the application picked: its ID, name and icon.
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
    // The list, or in its place a word on why there is none.
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

}  // namespace lsw::host::manager
