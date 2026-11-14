#pragma once

#include <QFrame>

class QLabel;
class QRadioButton;

namespace lsw::host::ui {
class ChoiceCard : public QFrame {
    Q_OBJECT
public:
    ChoiceCard(const QString& title, const QString& description, QWidget* parent = nullptr);

    void set_selected(bool selected);
    [[nodiscard]] bool selected() const;

    void set_in_use(bool in_use);
    [[nodiscard]] bool in_use() const;

    void set_waiting(bool waiting);
    [[nodiscard]] bool waiting() const;

Q_SIGNALS:
    void chosen();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    QRadioButton* dial_ = nullptr;
    QLabel* in_use_ = nullptr;
    QLabel* waiting_ = nullptr;
};

}
