#pragma once

#include <QPoint>
#include <QRect>

namespace lsw::host::manager {

// The guest window's system caption buttons, in captured-frame pixels.
//
// Presses land on these constantly, so the check has to be local: an RPC
// per click to ask the guest what was under the cursor would make the
// title bar feel broken. The rects come from UIA once, at window_props
// time, and are kept aligned afterwards by tracking the frame width —
// the buttons are top-right anchored, so a wider window shifts them
// right by exactly the width delta.
class CaptionButtonRects {
public:
    enum class Button { None, Minimize, Maximize, Close, Help };

    // Adopt rects reported by ``window_props``, measured against a frame
    // ``efb_width`` wide. An empty rect means the window lacks that
    // button.
    void adopt(QRect minimize, QRect maximize, QRect close, QRect help, int efb_width);

    // Re-anchor to a new captured-frame width. No-op until ``adopt``
    // has run or when the width is unchanged.
    void track_frame_width(int efb_width);

    // Which button contains ``guest_point``, if any.
    [[nodiscard]] Button hit(QPoint guest_point) const;

    [[nodiscard]] const QRect& minimize() const noexcept { return minimize_; }
    [[nodiscard]] const QRect& maximize() const noexcept { return maximize_; }
    [[nodiscard]] const QRect& close() const noexcept { return close_; }
    [[nodiscard]] const QRect& help() const noexcept { return help_; }

private:
    QRect minimize_;
    QRect maximize_;
    QRect close_;
    QRect help_;
    // Frame width the rects currently correspond to; 0 until adopted.
    int anchored_width_ = 0;
};

}  // namespace lsw::host::manager
