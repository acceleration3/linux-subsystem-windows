#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

class QPainter;

namespace lsw::host::manager {

// How a mirror gets guest pixels onto the screen.
//
// The project has two ways of producing those pixels and, until this
// interface existed, two unrelated implementations of "a host window
// showing one guest application":
//
//   * the manager's mirrors — captured BGRA frames delivered through the
//     IVSHMEM frame ring and blitted by Qt (``ShmemPresenter``), and
//   * the render server — a replayed Vulkan command stream, drawn into
//     an xcb window it creates for itself.
//
// They are the same job. This is the seam between them: a ``WindowView``
// owns a ``Presenter`` and knows nothing about which one it has, so the
// backend becomes a per-window runtime choice rather than a fork of the
// codebase. See ``VulkanPresenter`` for the second implementation's
// intended shape.
//
// The interface is in guest-pixel terms on the way in and widget-pixel
// terms on the way out; ``FrameGeometry`` does the arithmetic in
// between.
class Presenter {
public:
    virtual ~Presenter() = default;

    Presenter() = default;
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;
    Presenter(Presenter&&) = delete;
    Presenter& operator=(Presenter&&) = delete;

    // True once there is something to draw.
    [[nodiscard]] virtual bool has_content() const = 0;

    // Size of the guest content, in guest pixels.
    [[nodiscard]] virtual QSize source_size() const = 0;

    // The sub-rectangle of the source that is actually the window —
    // popups trim a transparent shadow margin. Empty means "all of it".
    [[nodiscard]] virtual QRect content_rect() const = 0;

    // Where the content lands inside a widget of ``widget_size``.
    [[nodiscard]] virtual QRect frame_rect(QSize widget_size) const = 0;

    // Widget-relative position → guest pixel.
    [[nodiscard]] virtual QPoint map_to_guest(QPointF widget_position,
                                              QSize widget_size) const = 0;

    // Draw. ``translucent`` selects the popup treatment: clear to
    // transparent rather than filling black, so the trimmed shadow
    // margin does not paint as a box.
    virtual void paint(QPainter& painter, QSize widget_size, bool translucent) const = 0;
};

}  // namespace lsw::host::manager
