#pragma once

#include <QString>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;

namespace lsw::host::apps {
class AppConfigStore;
}

namespace lsw::host::manager {
class RemovedAppsPopup;

class RemovedAppsBar : public QWidget {
    Q_OBJECT
public:
    static constexpr int kUndoMs = 10000;

    explicit RemovedAppsBar(apps::AppConfigStore& store, QWidget* parent = nullptr);

    void removed(const QString& id);

Q_SIGNALS:
    void restored(const QString& id);

private:
    void refresh();
    void bring_back(const QString& id);

    apps::AppConfigStore& store_;
    QLabel* text_ = nullptr;
    QPushButton* undo_ = nullptr;
    QPushButton* restore_ = nullptr;
    RemovedAppsPopup* popup_ = nullptr;
    QTimer undo_window_;
    QString last_;
};

}  // namespace lsw::host::manager
