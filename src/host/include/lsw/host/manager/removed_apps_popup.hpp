#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace lsw::host::apps {
class AppConfigStore;
}

namespace lsw::host::manager {
class RemovedAppsPopup : public QFrame {
    Q_OBJECT
public:
    static constexpr int kFindFrom = 9;
    static constexpr int kVisibleRows = 10;

    explicit RemovedAppsPopup(apps::AppConfigStore& store, QWidget* parent = nullptr);

    void open_above(QWidget* anchor);

Q_SIGNALS:
    void restored(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void fill();
    void restore_one(const QString& id);
    void restore_shown();

    apps::AppConfigStore& store_;
    QLabel* title_ = nullptr;
    QLineEdit* find_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* none_ = nullptr;
    QPushButton* all_ = nullptr;
};

}  // namespace lsw::host::manager
