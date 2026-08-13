#pragma once

#include <QObject>
#include <QSize>
#include <QTimer>

#include <functional>

namespace lsw::host::manager {

// Keeps a mirror's size and its guest window's size in agreement,
// without the two chasing each other.
//
// Three separate hazards live here, which is why they live together:
//
//   * ECHO. Sizing the widget to a guest-reported frame fires
//     ``resizeEvent``; forwarding that back as a resize command would
//     tell the guest what it just told us. ``expect_echo`` marks the
//     next resize as ours.
//   * EARLINESS. Qt lays the widget out before the guest's properties
//     arrive. A resize sent then shrinks the guest to an arbitrary
//     default rectangle.
//   * CHATTER. ``resizeEvent`` fires continuously during a drag, so only
//     the final size is sent, after a debounce.
//
// Plus the clamp: the user may drag the mirror past the guest's own
// desktop, and the guest must never be told to be larger than its
// screen — the frame letterboxes inside the bigger widget instead.
class GeometrySync : public QObject {
    Q_OBJECT
public:
    // Called with the size the guest window should take, in guest pixels.
    using ResizeSender = std::function<void(int width, int height)>;

    GeometrySync(ResizeSender send, QObject* parent = nullptr);

    // The guest's properties have been applied: resizes now count.
    void mark_ready() { ready_ = true; }
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // About to size the widget from a frame — suppress the resulting
    // resizeEvent.
    void expect_echo() { expect_echo_ = true; }

    // The guest's primary screen size; zero (the default) disables the
    // clamp until the properties arrive.
    void set_guest_screen(int width, int height);

    // Feed every ``resizeEvent``. Returns false when the event was our
    // own echo or arrived too early, i.e. nothing was scheduled.
    bool widget_resized(QSize widget_size);

private Q_SLOTS:
    void flush();

private:
    ResizeSender send_;
    QTimer debounce_;
    QSize pending_;
    bool ready_ = false;
    bool expect_echo_ = false;
    int guest_screen_width_ = 0;
    int guest_screen_height_ = 0;
};

}  // namespace lsw::host::manager
