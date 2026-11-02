#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QPushButton;
class QWidget;

namespace lsw::host::services { class LogCollector; }

namespace lsw::host::manager {

class CollectLogsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mark { Pending, Running, Done, Skipped, Failed };

    explicit CollectLogsDialog(services::LogCollector& collector, QWidget* parent = nullptr);

private:
    void restart();
    void redraw();
    void set_mark(int step, Mark mark, const QString& note = {});

    services::LogCollector& collector_;
    QVector<Mark> marks_;
    QVector<QString> notes_;
    QString archive_;
    QString error_;

    QWidget* steps_ = nullptr;
    QWidget* result_ = nullptr;
    QLabel* path_ = nullptr;
    QLabel* failure_ = nullptr;
    QPushButton* copy_ = nullptr;
    QPushButton* show_ = nullptr;
    QPushButton* retry_ = nullptr;
    QPushButton* close_ = nullptr;
};

}
