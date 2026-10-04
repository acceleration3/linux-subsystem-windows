#pragma once

#include <QMargins>

class QBoxLayout;
class QDialog;
class QHBoxLayout;
class QVBoxLayout;

namespace lsw::host::ui {

// The frame a dialog's content sits in: a padded body, and under it a
// button band on the surface colour with a rule over it.
//
// The body carries the dialog's padding; the band beneath it runs edge
// to edge, which is what makes it a band.
struct DialogChrome {
    QVBoxLayout* body = nullptr;
    QHBoxLayout* foot = nullptr;

    // Lays ``dialog`` out as a body padded 20/18/20/16 with 14 between
    // rows, over a band padded 20/12.
    [[nodiscard]] static DialogChrome install(QDialog* dialog);

    // Just the band, appended to ``into``, for windows whose body is
    // something other than a padded column: a file browser, a console.
    // A negative ``spacing`` keeps the style's own.
    [[nodiscard]] static QHBoxLayout* foot_band(QBoxLayout* into, const QMargins& padding,
                                                int spacing = -1);
};

}  // namespace lsw::host::ui
