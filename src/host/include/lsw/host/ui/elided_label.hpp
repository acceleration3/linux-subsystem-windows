#pragma once

#include <QLabel>

namespace lsw::host::ui {
class ElidedLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElidedLabel(const QString& text = {}, QWidget* parent = nullptr);

    void set_full_text(const QString& text);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void elide();

    QString full_;
};

}
