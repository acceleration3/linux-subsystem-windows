#pragma once

#include <QWidget>

class QHBoxLayout;
class QSlider;
class QSpinBox;

namespace lsw::host::ui {
class SliderSpin : public QWidget {
    Q_OBJECT
public:
    SliderSpin(int min, int max, int step, const QString& suffix, QWidget* parent = nullptr);

    [[nodiscard]] int value() const;
    void set_value(int value);

    void set_minimum(int min);
    void set_maximum(int max);

    [[nodiscard]] QSpinBox* spin() const noexcept { return spin_; }
    [[nodiscard]] QHBoxLayout* row() const noexcept { return row_; }

Q_SIGNALS:
    void value_changed(int value);

private:
    QHBoxLayout* row_ = nullptr;
    QSlider* slider_ = nullptr;
    QSpinBox* spin_ = nullptr;
};

}
