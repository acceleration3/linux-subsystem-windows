#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace lsw::host::manager {
class WarningStrip : public QWidget {
    Q_OBJECT
public:
    explicit WarningStrip(QWidget* parent = nullptr);

    void set_message(bool critical, const QString& headline, const QString& detail);

    QPushButton* add_button(const QString& text, const char* role = nullptr);
    void clear_buttons();
    void add_dismiss();

Q_SIGNALS:
    void dismissed();

private:
    QLabel* dot_ = nullptr;
    QLabel* text_ = nullptr;
    QHBoxLayout* buttons_ = nullptr;
};

}
