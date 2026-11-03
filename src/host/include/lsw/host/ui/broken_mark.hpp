#pragma once

#include <QString>
#include <QWidget>

class QEvent;
class QPaintEvent;

namespace lsw::host::ui {
class BrokenMark final : public QWidget {
public:
    static void apply(QWidget* panel);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit BrokenMark(QWidget* panel);

    QWidget* panel_;
};

}
