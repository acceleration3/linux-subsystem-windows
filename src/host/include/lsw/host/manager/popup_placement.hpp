#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

namespace lsw::host::manager {

// Places an owned popup mirror relative to its owner.
//
// The offset the guest reports is measured from the owner's CLIENT
// origin, which on the host is the owner's content (0,0) — not its
// window position, which includes any frame and shifted every popup
// down and to the right. It also has to be scaled by the owner's current
// display scale, since the owner is not necessarily shown 1:1.
class PopupPlacement {
public:
    PopupPlacement() = delete;

    // ``owner_origin`` is the owner's mapToGlobal({0,0}).
    // ``owner_frame`` / ``owner_image`` give the owner's current scale;
    // a null image means scale 1.0, which is the right answer before the
    // owner's first frame has arrived.
    [[nodiscard]] static QPoint anchor(QPoint owner_origin, QRect owner_frame,
                                       QSize owner_image, QPoint relative_guest_px);
};

}  // namespace lsw::host::manager
