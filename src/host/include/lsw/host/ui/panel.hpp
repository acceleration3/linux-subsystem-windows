#pragma once

#include <QString>

class QFrame;
class QHBoxLayout;
class QVBoxLayout;

namespace lsw::host::ui {

// A bordered frame with a header band across its top and a body under
// it, rather than a QGroupBox with its title floating on the border.
// The difference is that the header is a row: it can carry buttons, a
// size, or a status pill beside the name.
//
// The header is padded 12/8 and holds the uppercase caption when one is
// given; the body is a column padded 12 with 10 between rows. Pages that
// want something else set it on the layouts they get back.
struct Panel {
    QFrame* frame = nullptr;
    QHBoxLayout* header = nullptr;
    QVBoxLayout* body = nullptr;

    // ``caption`` empty leaves the header row empty for the caller.
    [[nodiscard]] static Panel make(const QString& caption = {});
};

}  // namespace lsw::host::ui
