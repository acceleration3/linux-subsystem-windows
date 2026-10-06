#pragma once

class QPainter;
class QRect;

namespace lsw::host::ui {

// A preview's play or pause control, as the design draws it: a 56px disc
// with two bars, and a mono caption under it, centred in the surface.
// Paused, it is the badge that says the picture is still, and it turns
// amber under the pointer (``hot``) because it is the thing that plays.
class PlayBadge {
public:
    PlayBadge(bool paused, bool hot) noexcept;

    void paint(QPainter& p, const QRect& area) const;

private:
    bool paused_;
    bool hot_;
};

}  // namespace lsw::host::ui
