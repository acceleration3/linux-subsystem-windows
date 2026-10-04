#include "lsw/host/ui/slider_spin.hpp"

#include <QHBoxLayout>
#include <QSlider>
#include <QSpinBox>

namespace lsw::host::ui {

SliderSpin::SliderSpin(int min, int max, int step, const QString& suffix, QWidget* parent)
    : QWidget(parent) {
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(0, 0, 0, 0);

    slider_ = new QSlider(Qt::Horizontal);
    slider_->setRange(min, max);
    slider_->setSingleStep(step);
    slider_->setPageStep(step * 4);

    spin_ = new QSpinBox;
    spin_->setRange(min, max);
    spin_->setSingleStep(step);
    spin_->setSuffix(suffix);

    connect(slider_, &QSlider::valueChanged, spin_, &QSpinBox::setValue);
    connect(spin_, &QSpinBox::valueChanged, slider_, &QSlider::setValue);
    connect(spin_, &QSpinBox::valueChanged, this, &SliderSpin::value_changed);

    row_->addWidget(slider_, 1);
    row_->addWidget(spin_);
}

int SliderSpin::value() const {
    return spin_->value();
}

void SliderSpin::set_value(int value) {
    spin_->setValue(value);
    slider_->setValue(value);
}

void SliderSpin::set_maximum(int max) {
    spin_->setMaximum(max);
    slider_->setMaximum(max);
}

}  // namespace lsw::host::ui
