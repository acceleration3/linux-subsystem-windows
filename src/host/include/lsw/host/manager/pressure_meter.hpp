// A bar that draws two readings of the same area at once.
//
// An area has a depth now and a depth at its worst, and they answer
// different questions: the first says what is happening, the second
// says how big the area needs to be. A bar can only fill to one of
// them, and picking either one loses the other — a bar drawn from the
// peak climbs once and then sits still for the session, and a bar drawn
// from the live figure never shows that the arena was nearly full a
// minute ago.
//
// So it draws both. The peak is a paler band of the same colour behind,
// the live figure a solid one in front: the same measurement at two
// moments, which is what the shared hue says and two unrelated colours
// would not.
#pragma once

#include <QColor>
#include <QWidget>

namespace lsw::host::manager {

class PressureMeter : public QWidget {
    Q_OBJECT
    // Set from the stylesheet, so the colours stay where the rest of
    // the theme's colours are. `fill` is the pressure colour and
    // changes with the `pressure` property; the peak band is derived
    // from it rather than named separately, because it is the same
    // reading and wants the same hue.
    Q_PROPERTY(QColor track READ track WRITE set_track)
    Q_PROPERTY(QColor fill READ fill WRITE set_fill)

public:
    explicit PressureMeter(QWidget* parent = nullptr);

    // Both as percentages of the area's capacity, clamped to 0..100.
    void set_readings(int used_percent, int peak_percent);

    // A bar is as tall as it is told to be and as wide as it is given.
    // Stated here rather than left to the stylesheet: a QWidget with no
    // size hint collapses to nothing in a layout, and a rule that fails
    // to match then takes the whole bar with it rather than just its
    // colours.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    [[nodiscard]] QColor track() const noexcept;
    void set_track(const QColor& colour);
    [[nodiscard]] QColor fill() const noexcept;
    void set_fill(const QColor& colour);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor track_{0x20, 0x25, 0x2f};
    QColor fill_{0x4e, 0xc9, 0x8a};
    int used_ = 0;
    int peak_ = 0;
};

}  // namespace lsw::host::manager
