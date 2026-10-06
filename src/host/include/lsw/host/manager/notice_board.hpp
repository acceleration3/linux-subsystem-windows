#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QVBoxLayout;

namespace lsw::host::manager {

class WarningStrip;

// The manager's notices: each thing it tried that did not work, said once
// as a warning strip across the top of the page, newest first, with what
// helps and a Dismiss. One strip per kind of failure, by key: posting the
// same key again replaces it, and withdrawing it when the failure stops
// being true takes it away.
class NoticeBoard : public QWidget {
    Q_OBJECT
public:
    struct Action {
        QString text;
        std::function<void()> run;
    };

    explicit NoticeBoard(QWidget* parent = nullptr);

    // ``critical`` when the guest cannot run because of it.
    void post(const QString& key, bool critical, const QString& headline, const QString& detail,
              const QVector<Action>& actions = {});
    void withdraw(const QString& key);

private:
    QVBoxLayout* column_;
    QHash<QString, WarningStrip*> strips_;
};

}  // namespace lsw::host::manager
