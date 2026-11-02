#pragma once

class QPainter;
class QRect;

namespace lsw::host::ui {
class PlayBadge {
public:
    PlayBadge(bool paused, bool hot) noexcept;

    void paint(QPainter& p, const QRect& area) const;

private:
    bool paused_;
    bool hot_;
};

}
