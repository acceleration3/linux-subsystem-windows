#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QVBoxLayout;

namespace lsw::host::manager {

class WarningStrip;

class NoticeBoard : public QWidget {
    Q_OBJECT
public:
    struct Action {
        QString text;
        std::function<void()> run;
    };

    explicit NoticeBoard(QWidget* parent = nullptr);

    void post(const QString& key, bool critical, const QString& headline, const QString& detail,
              const QVector<Action>& actions = {});
    void withdraw(const QString& key);

private:
    QVBoxLayout* column_;
    QHash<QString, WarningStrip*> strips_;
};

}
