#pragma once

#include "lsw/host/manager/presenter.hpp"

#include <QImage>

namespace lsw::host::manager {

// Presents captured frames: the agent commits BGRA into the IVSHMEM
// frame ring, the manager reads a QImage out of it, this draws it.
//
// This is the behaviour the mirrors have always had, lifted out of
// ``WindowView`` so the widget holds a ``Presenter`` rather than a
// QImage plus the arithmetic that goes with it.
class ShmemPresenter : public Presenter {
public:
    // ``trim_shadow`` is set for popups: WinUI menus arrive with a
    // transparent drop-shadow margin that must be cropped away, or the
    // menu floats in the middle of an oversized window with a second,
    // compositor-drawn shadow around it. Classic Win32 menus have no
    // such margin and the trim comes back empty.
    //
    // For anything else the content rect is not guessed from the pixels
    // — the guest already knows it and says so; see ``set_content_rect``.
    void submit(QImage frame, bool trim_shadow);

    // The sub-rect of the frame to show, as the guest reported it: the
    // whole frame, except for a maximized window whose frame overhangs
    // the monitor. An empty rect means the whole frame.
    //
    // Deliberately not derived from alpha here. The transparent band on
    // a maximized window looks exactly like the transparent band on a
    // frame that arrived half-painted mid-resize, and cropping to the
    // pixels cropped real content the moment the two were confused.
    void set_content_rect(QRect rect);

    [[nodiscard]] bool has_content() const override { return !image_.isNull(); }
    [[nodiscard]] QSize source_size() const override { return image_.size(); }
    [[nodiscard]] QRect content_rect() const override { return content_rect_; }
    [[nodiscard]] QRect frame_rect(QSize widget_size) const override;
    [[nodiscard]] QPoint map_to_guest(QPointF widget_position,
                                      QSize widget_size) const override;
    void paint(QPainter& painter, QSize widget_size, bool letterbox) const override;

    // The image itself, for the paths that still need it: caption-button
    // width tracking and the non-client hit-test debug overlay.
    [[nodiscard]] const QImage& image() const noexcept { return image_; }

    // Size the widget should take to show this frame 1:1 — the trimmed
    // content for a popup, the whole frame otherwise.
    [[nodiscard]] QSize natural_size() const;

private:
    QImage image_;
    QRect content_rect_;  // empty = use the whole frame
};

}  // namespace lsw::host::manager
