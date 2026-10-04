#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace lsw::host::manager {

// A band across the top of a page saying one thing is wrong and what
// to do about it: a dot, a sentence whose first clause is the headline,
// and the buttons that act on it. Amber for something to act on, red
// for something that breaks the guest.
class WarningStrip : public QWidget {
    Q_OBJECT
public:
    explicit WarningStrip(QWidget* parent = nullptr);

    void set_message(bool critical, const QString& headline, const QString& detail);

    // A button at the strip's right end, in the order added.
    QPushButton* add_button(const QString& text, const char* role = nullptr);
    // Drop every button, for a strip that is about something else now.
    void clear_buttons();

private:
    QLabel* dot_ = nullptr;
    QLabel* text_ = nullptr;
    QHBoxLayout* buttons_ = nullptr;
};

}  // namespace lsw::host::manager
