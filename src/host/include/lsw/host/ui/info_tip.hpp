#pragma once

#include <QString>
#include <QWidget>

namespace lsw::host::ui {
class InfoTip : public QWidget {
    Q_OBJECT
public:
    explicit InfoTip(const QString& text = {}, QWidget* parent = nullptr);

    void set_text(const QString& text);
    void append(const QString& text);

    [[nodiscard]] const QString& text() const noexcept { return text_; }

    [[nodiscard]] QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void show_tip();
    void show_if_placed();

    QString text_;
    bool lit_ = false;
};

}  // namespace lsw::host::ui
