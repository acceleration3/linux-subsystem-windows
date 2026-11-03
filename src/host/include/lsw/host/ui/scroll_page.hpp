#pragma once

#include <QWidget>

class QScrollArea;
class QVBoxLayout;

namespace lsw::host::ui {
class ScrollPage : public QWidget {
    Q_OBJECT
public:
    explicit ScrollPage(QWidget* parent = nullptr);

    [[nodiscard]] QVBoxLayout* column() const noexcept { return column_; }

    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QScrollArea* scroll_ = nullptr;
    QWidget* holder_ = nullptr;
    QVBoxLayout* column_ = nullptr;
};

}
