#pragma once

#include <QAbstractButton>

namespace lsw::host::ui {

// The pill switch the settings page is built around.
//
// A check box says "this is one of a list of things you are ticking".
// A switch says "this is on, and turning it off is one click". Every
// boolean in Settings is the second kind, and the design draws them as
// switches, so they are switches.
//
// Painted rather than stylesheeted: the knob has to travel, and a
// stylesheet has nowhere to put a moving part.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

}  // namespace lsw::host::ui
