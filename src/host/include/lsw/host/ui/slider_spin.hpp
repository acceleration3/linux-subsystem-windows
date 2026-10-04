#pragma once

#include <QWidget>

class QHBoxLayout;
class QSlider;
class QSpinBox;

namespace lsw::host::ui {

// A slider with the number it says beside it.
//
// The spin box owns the value; the slider mirrors it, and moving either
// moves the other. The slider is the control, the spin box is where the
// value can be typed exactly, and both always agree on the range.
class SliderSpin : public QWidget {
    Q_OBJECT
public:
    SliderSpin(int min, int max, int step, const QString& suffix, QWidget* parent = nullptr);

    [[nodiscard]] int value() const;
    void set_value(int value);

    // Narrow the range to what this host can give. Both halves follow,
    // so the slider cannot travel past what the number will accept.
    void set_maximum(int max);

    // For the pages that dress the number differently.
    [[nodiscard]] QSpinBox* spin() const noexcept { return spin_; }
    [[nodiscard]] QHBoxLayout* row() const noexcept { return row_; }

Q_SIGNALS:
    void value_changed(int value);

private:
    QHBoxLayout* row_ = nullptr;
    QSlider* slider_ = nullptr;
    QSpinBox* spin_ = nullptr;
};

}  // namespace lsw::host::ui
